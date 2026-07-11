// Area.cpp

#include <RCU.hpp>
#include <variant>
#include <Global.h>
#include <VirtualRouter.h>

#include "Area.h"
#include "IntraOriginator.h"
#include "ospf/OspfProcess.h"
#include "ospf/OspfTypes.hpp"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/neighbor/NeighborTable.h"
#include "configs/registry/router/OspfRegistry.h"
#include "ospf/transmission/PacketDispatcher.h"

namespace routing::ospf
{
Area::Area(OspfProcess& base, uint32_t id)
    : process(base),
      areaId(id),
      spfMgr(*this, process.rib),
      flags(base.isV3),
      floodMgr(*this),
      scheduler(base.schedulerMgr.ref()),
      originContext(*this),
      originator(IntraOriginator::create(originContext)),
      routeManager(*this),
      configs(base.configs.get<config::Ospf::AREA_CONFIGS>().emplaceBack(id)),
      priv(*this)
{
    priv.type = configs.get<config::OspfArea::AREA_TYPE>().load();

    bool isStub = (priv.type == config::ospf::AreaType::STUB ||
                   priv.type == config::ospf::AreaType::TOTALLY_STUB);
    bool isNssa = (priv.type == config::ospf::AreaType::NSSA ||
                   priv.type == config::ospf::AreaType::TOTALLY_NSSA);
    flags.setExternalRouting(!isStub);
    flags.setNssa(isNssa);

    configs.context().set(this);
    priv.startAgingTimer();
}

Area::Private::Private(Area& a)
    : area(a)
{}

Area::~Area()
{
    if (priv.ignoreTid != 0)
        scheduler.cancel(priv.ignoreTid);
    if (priv.resetTid != 0)
        scheduler.cancel(priv.resetTid);
    if (priv.agingTimerId != 0)
        scheduler.cancel(priv.agingTimerId);
    process.configs.get<config::Ospf::AREA_CONFIGS>().erase(areaId);
    delete &originator;
}

void Area::enqueueReset()
{
    scheduler.post([this] {
        reset();
    });
}

void Area::enqueueSyncRanges()
{
    scheduler.post([this] {
        syncRangeConfig();
    });
}

void Area::reloadType()
{
    auto newType = configs.get<config::OspfArea::AREA_TYPE>().load();
    if (newType == priv.type) return;

    priv.type.store(newType, std::memory_order_release);

    bool isStub = (priv.type == config::ospf::AreaType::STUB ||
                   priv.type == config::ospf::AreaType::TOTALLY_STUB);
    bool isNssa = (priv.type == config::ospf::AreaType::NSSA ||
                   priv.type == config::ospf::AreaType::TOTALLY_NSSA);

    flags.setExternalRouting(!isStub);
    flags.setNssa(isNssa);
}

void Area::reset()
{
    reloadType();

    // Reset all neighbors on all interfaces in this area
    process.ifaceMgr.resetNeighbors();

    // Flush and clear LSDB
    // MaxAge-flood all LSAs so neighbors know we're resetting
    for (auto& [key, record] : lsdb.getIterableLSDB())
    {
        record.header.age = OSPF_MAX_AGE;
        FloodInfo info{FloodReason::FLUSH};
        floodMgr.enqueueFlood(LsaRecordRef{key, record}, info);
    }
    lsdb.clear();

    // Re-originate all self-originated LSAs
    originator.fullRefresh();
}

void Area::flushNeighborLsas(uint32_t neighborRid)
{
    // Collect all LSA keys originated by this neighbor
    std::vector<LsaKey> toFlush;
    lsdb.forEach([&](const LsaKey& key, const LsaRecord&) {
        if (key.advertisingRouter == neighborRid)
            toFlush.push_back(key);
    });

    for (const auto& key : toFlush)
    {
        LsaRecord* record = lsdb.find(key);
        if (!record) continue;

        record->header.age = OSPF_MAX_AGE;
        FloodInfo info{FloodReason::FLUSH};
        floodMgr.enqueueFlood(LsaRecordRef{key, *record}, info);
    }

    if (!toFlush.empty())
    {
        spfMgr.requestSpf();
    }
}

void Area::clear()
{
    lsdb.clear();
}

void Area::releaseMemory()
{
    lsdb.releaseMemory();
}

void Area::runDCIntegrityScan()
{
    bool enabled = lsdb.runDCIntegrityScan();
    bool old = priv.dcCompatible.exchange(enabled, std::memory_order_acq_rel);
    if (old != enabled)
    {
        process.ifaceMgr.runDCIntegrityScan(enabled);
    }
}

bool Area::isValidForwardAddress(const types::IPAddress& addr) const
{
    if ((priv.type == config::ospf::AreaType::NSSA || priv.type == config::ospf::AreaType::TOTALLY_NSSA) &&
        configs.get<config::OspfArea::NSSA_SUPPRESS_FA>().load())
        return false;

    if (process.configs.get<config::Ospf::LRC_FORWARDING_ADDRESS>().load())
    {
        utils::RCU::Guard g;
        return addr.isIPv6()
            ? process.routingInstance->getRib().lookup(addr.v6(), g) != nullptr
            : process.routingInstance->getRib().lookup(addr.v4(), g) != nullptr;
    }
    else
    {
        return process.rib.lpmLookup(addr, areaId);
    }
}

void Area::syncRangeConfig()
{
    auto cfgRanges = configs.get<config::OspfArea::RANGE>();
    std::unordered_set<types::IPPrefix> activeRanges;
    priv.rangePrefixes.clear();

    cfgRanges.withRead([&](const auto& tsList)
    {
        for (const auto& t : tsList)
        {
            const auto& [pfx, noAdv, cost] = t;
            priv.rangePrefixes.insert(pfx);

            auto& r = priv.ranges[pfx];
            r.notAdvertise = noAdv;
            r.costOverride = cost;
        }
    });

    for (auto it = priv.ranges.begin(); it != priv.ranges.end();)
    {
        if (priv.rangePrefixes.find(it->first) == priv.rangePrefixes.end())
            it = priv.ranges.erase(it);
        else
            ++it;
    }

    for (const auto& [r, _] : priv.ranges)
        activeRanges.insert(r);

    syncRangeSuppression(activeRanges);
}

void Area::syncRangeSuppression(const std::unordered_set<types::IPPrefix>& activeRanges, bool abrChange)
{
    bool isABR = process.isABR();

    auto changes = isABR
        ? process.rib.refreshIntraRangeSuppression(areaId, activeRanges)
        : process.rib.refreshIntraRangeSuppression(areaId, {});

    if (process.isV3)
        process.interOriginator.reoriginateSummaries<PolicyV3>(originContext, changes);
    else
        process.interOriginator.reoriginateSummaries<PolicyV2>(originContext, changes);

    if (isABR || abrChange)
    {
        std::vector<std::pair<types::IPPrefix, OspfPath>> intra = isABR
            ? process.rib.getIntraAreaRoutes(areaId)
            : std::vector<std::pair<types::IPPrefix, OspfPath>>{};

        syncRangeRuntime(intra, abrChange);
    }
}

void Area::syncRangeRuntime(const std::vector<std::pair<types::IPPrefix, OspfPath>>& intraRoutes, bool abrChange)
{
    struct SummaryAction
    {
        uint32_t lsid;
        types::IPPrefix pfx;
        uint32_t metric;
        bool flush;
    };
    struct DiscardAction
    {
        types::IPPrefix pfx;
        uint32_t metric;
        bool install;
    };

    std::vector<SummaryAction> summaryActions;
    std::vector<DiscardAction> discardActions;

    // If not ABR: withdraw any previously originated range summaries + discards, but do NOT just clear silently.
    if (!process.isABR())
    {
        if (!abrChange) return;

        // Build withdrawals from existing runtime state
        for (auto& [pfx, r] : priv.ranges)
        {
            if (r.summary.has_value())
            {
                summaryActions.push_back({ r.summary.value(), pfx, 0, true });
                r.summary = std::nullopt;
            }

            if (r.discardPresent)
            {
                discardActions.push_back({ pfx, 0, false });
                r.discardPresent = false;
            }

            r.contributorCount = 0;
            r.computedMetric = 0;
        }
    }
    else
    {
        // ABR case: compute contributors and update runtime state
        auto rcs = computeRangeContributors(intraRoutes, priv.ranges);

        for (auto& [pfx, r] : priv.ranges)
        {
            auto it = rcs.find(pfx);
            uint32_t count = 0;
            uint32_t minMetric = 0;
            if (it != rcs.end())
            {
                count = it->second.first;
                minMetric = it->second.second;
            }

            const bool active = count > 0;
            const uint32_t metric = r.costOverride ? *r.costOverride : minMetric;

            const bool shouldAdvertise = active && !r.notAdvertise;
            const bool shouldDiscard   = active;

            // Summary
            if (!r.summary.has_value() && shouldAdvertise)
            {
                const uint32_t lsid = process.isV3
                    ? getInterOriginator().fetchAddMonotonicIntraId()
                    : pfx.v4();

                r.summary = lsid;
                summaryActions.push_back({ lsid, pfx, metric, false });
            }
            else if (r.summary.has_value() && !shouldAdvertise)
            {
                summaryActions.push_back({ r.summary.value(), pfx, 0, true });
                r.summary = std::nullopt;
            }
            else if (r.summary.has_value() && shouldAdvertise && r.computedMetric != metric)
            {
                summaryActions.push_back({ r.summary.value(), pfx, metric, false });
            }

            // Discard (must follow shouldDiscard, not shouldAdvertise)
            if (!r.discardPresent && shouldDiscard)
            {
                discardActions.push_back({ pfx, metric, true });
                r.discardPresent = true;
            }
            else if (r.discardPresent && !shouldDiscard)
            {
                discardActions.push_back({ pfx, 0, false });
                r.discardPresent = false;
            }
            else if (r.discardPresent && shouldDiscard && r.computedMetric != metric)
            {
                discardActions.push_back({ pfx, metric, true });
            }

            r.contributorCount = count;
            r.computedMetric = active ? metric : 0;
        }
    }

    for (const auto& a : summaryActions)
    {
        process.isV3
            ? getInterOriginator().originateSummary<PolicyV3>(originContext, a.lsid, a.pfx, a.metric, a.flush)
            : getInterOriginator().originateSummary<PolicyV2>(originContext, a.lsid, a.pfx, a.metric, a.flush);
    }

    if (process.configs.get<config::Ospf::DISCARD_INTERNAL>().load())
    {
        for (const auto& d : discardActions)
        {
            if (d.install)
            {
                const uint8_t ad = process.configs.get<config::Ospf::DISCARD_INTERNAL_DISTANCE>().load();
                process.rib.installDiscardRoute({ d.pfx, areaId }, d.metric, ad);
            }
            else
            {
                process.rib.withdrawDiscardRoute({ d.pfx, areaId });
            }
        }
    }
}

const std::unordered_set<types::IPPrefix>& Area::getRanges() const
{
    return priv.rangePrefixes;
}

void Area::suppressInterAreaPrefix(const types::IPPrefix& prefix)
{
    auto it = priv.ranges.find(prefix);
    if (it == priv.ranges.end() || !it->second.summary.has_value())
        return;

    process.isV3
        ? getInterOriginator().originateSummary<PolicyV3>(originContext, it->second.summary.value(), prefix, 0, true)
        : getInterOriginator().originateSummary<PolicyV2>(originContext, it->second.summary.value(), prefix, 0, true);
}

std::unordered_map<types::IPPrefix, std::pair<uint32_t, uint32_t>> Area::computeRangeContributors(
    const std::vector<std::pair<types::IPPrefix, OspfPath>>& intraAreaRoutes,
    const std::unordered_map<types::IPPrefix, AreaRange>& activeRanges)
{
    std::unordered_map<types::IPPrefix, std::pair<uint32_t, uint32_t>> rcs;

    rcs.reserve(activeRanges.size());
    for (const auto& [pfx, _] : activeRanges)
        rcs.try_emplace(pfx, 0, std::numeric_limits<uint32_t>::max());

    for (const auto& [pfx, path] : intraAreaRoutes)
    {
        if (path.suppressed || path.discard || path.type != OspfRouteType::INTRA_AREA) continue;
        if (!activeRanges.contains(pfx))
            continue;

        auto& r = rcs[pfx];
        r.first++;
        r.second = std::min(r.second, static_cast<uint32_t>(path.cost));
    }

    return rcs;
}

void Area::send(std::vector<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    process.ifaceMgr.broadcastLsu(*this, records);
}

template <typename Policy>
std::optional<Area::Result> Area::processLsa(IncomingLsaContext& ctx, LsaBody& body)
{
    if (!priv.preProcess<Policy>(ctx, body)) return std::nullopt;
    auto result = priv.process(ctx, body);

    priv.installLsa(result, ctx, body);
    priv.evaluateDecision(result, ctx);
    priv.postProcess<Policy>(result, ctx, body);

    return result;
}

template <typename Policy>
std::optional<Area::Result> Area::processLsa(IncomingLsaContext& ctx, const LsaBody& body)
{
    priv.preProcess<Policy>(ctx, body);
    auto result = priv.process(ctx, body);

    priv.evaluateDecision(result, ctx);
    priv.postProcess<Policy>(result, ctx, body);

    return result;
}

template <typename Policy>
void Area::processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries)
{
    lsdb.forEachInType(Policy::InterNetworkType, [&](const LsaKey& key, const LsaRecord& record) {
        if (!summaries.contains(key))
            originContext.originateLsa<Policy>(key, record.body, true);
    });

    for (auto& [key, body] : summaries)
    {
        originContext.originateLsa<Policy>(key, body, false);
    }
}

template <typename Policy>
void Area::processExternalLsa(IncomingLsaContext& ctx, const LsaBody& body)
{
    if (!priv.preProcess<Policy>(ctx, body)) return;

    bool expire = ctx.header.age == OSPF_MAX_AGE;
    LsaBody bodyCopy = body;

    auto& external = std::get<typename Policy::ExternalLsa>(bodyCopy);
    if constexpr (std::is_same_v<Policy, PolicyV2>)
    {
        if (external.forwardingAddress != 0 && !isValidForwardAddress(types::IPAddress(external.forwardingAddress)))
            external.forwardingAddress = 0;
    }
    else
    {
        if (external.forwardingAddress.has_value() && !isValidForwardAddress(external.forwardingAddress.value()))
            external.forwardingAddress = std::nullopt;
    }

    // Manage type 4 if needed
    if (priv.type.load(std::memory_order_relaxed) == config::ospf::AreaType::NORMAL)
        getInterOriginator().addExternal<Policy>(originContext, ctx.key.advertisingRouter, ctx.key.linkStateId, expire);

    auto result = priv.process(ctx, body);
    priv.installLsa(result, ctx, body);
    priv.evaluateDecision(result, ctx);
}

bool Area::compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const
{
    const LsaRecord* existing = lsdb.find(key);

    // We don't have this LSA at all — need to request it
    if (!existing)
        return true;

    // Neighbor has a newer version — need to request it
    const LsaCompareResult cmp = priv.compareLsaHeaders(hdr, existing->header);
    return cmp == LsaCompareResult::NEWER;
}

LsaRecordFlags Area::makeFlags(const IncomingLsaContext& ctx) noexcept
{
    LsaRecordFlags f = LsaRecordFlags::NONE;
    if (ctx.selfOriginatedKey) f |= LsaRecordFlags::SELF_ORIGINATED;
    if (ctx.checksumValid) f |= LsaRecordFlags::CHECKSUM_VALID;
    return f;
}

InterOriginator& Area::getInterOriginator()
{
    return process.interOriginator;
}

ExternalOriginator& Area::getExternalOriginator()
{
    return process.externalOriginator;
}

TopologyTable& Area::getTopoTable()
{
    return process.table;
}

const config::OspfRegistry& Area::getProcessConfigs() const
{
    return process.configs;
}

const InterfaceManager& Area::getIfaceMgr() const
{
    return process.ifaceMgr;
}

void Area::Private::evaluateDecision(Result& result, const IncomingLsaContext& ctx)
{
    if (result.decision.shouldFlood && result.record)
        area.floodMgr.enqueueFlood(LsaRecordRef{ctx.key, *result.record}, ctx.info);
    if (result.decision.affectsSpfGraph && result.decision.topologyChanged)
        area.spfMgr.requestSpf();
}

template <typename Policy>
bool Area::Private::preProcess(IncomingLsaContext& ctx, const LsaBody& body)
{
    bool isNssa = type == config::ospf::AreaType::NSSA || type == config::ospf::AreaType::TOTALLY_NSSA;
    if (std::holds_alternative<typename Policy::InterNetworkLsa>(body) &&
        (type == config::ospf::AreaType::TOTALLY_STUB || type == config::ospf::AreaType::TOTALLY_NSSA))
        return false;
    if (std::holds_alternative<typename Policy::InterRouterLsa>(body) && type != config::ospf::AreaType::NORMAL)
        return true;
    if (std::holds_alternative<typename Policy::ExternalLsa>(body))
    {
        //bool isRouteNssa = base.isNssaExternal<Policy>(ctx.header, std::get<typename Policy::ExternalLsa>(body));
        bool isRouteNssa = ctx.key.lsaType == Policy::NssaType;
        if (type == config::ospf::AreaType::NORMAL)
        {
            if (isRouteNssa) return false;
        }
        else if (isNssa)
        {
            if (!isRouteNssa) return false;
        }
        else return false;
    }
    return true;
}

template <typename Policy>
void Area::Private::postProcess(Result& result, IncomingLsaContext& ctx, const LsaBody& body)
{
    if (result.decision.action != InstallAction::REJECT_INVALID &&
        result.decision.action != InstallAction::IGNORE_OLDER &&
        result.decision.topologyChanged)
    {
        // Router and Network LSAs carry the DC options bit; re-check DC compatibility
        if (std::holds_alternative<typename Policy::RouterLsa>(body) ||
            std::holds_alternative<typename Policy::NetworkLsa>(body))
        {
            area.runDCIntegrityScan();
        }

        if (std::holds_alternative<typename Policy::ExternalLsa>(body))
        {
            area.getExternalOriginator().distributeExternalLsa<Policy>(area.originContext, ctx, body);
        }
        else if (std::holds_alternative<typename Policy::InterNetworkLsa>(body))
        {
            auto res = area.process.interRouteManager.deriveInterAreaNetwork<Policy>(area, ctx.key, ctx.header, body);
            auto changes = area.process.rib.replaceRoute(area, res);
            if (!changes.empty()) area.getInterOriginator().reoriginateSummaries<Policy>(area.originContext, changes);
        }
        else if (std::holds_alternative<typename Policy::InterRouterLsa>(body))
        {
            area.process.interRouteManager.deriveInterAreaRouter<Policy>(area, ctx.key, ctx.header, body);
        }
    }
}

Area::Result Area::Private::process(IncomingLsaContext& ctx, const LsaBody& body)
{
    Result out{};

    LsaRecord* existing = area.lsdb.find(ctx.key);

    out.decision = area.priv.evaluateIncomingLsa(existing, ctx, body);

    if (out.decision.action == InstallAction::REJECT_INVALID ||
        out.decision.action == InstallAction::IGNORE_OLDER)
    {
        // IGNORE_OLDER still acks (per RFC 2328 §13) using the existing, newer
        // stored instance; REJECT_INVALID never acks (shouldAck is false).
        out.record = existing;
        return out;
    }

    out.flags = makeFlags(ctx);

    if (out.decision.action == InstallAction::IGNORE_DUPLICATE)
    {
        if (out.decision.shouldUpdateAgeOnly)
        {
            if (auto* r = area.lsdb.find(ctx.key))
            {
                r->header.age = out.decision.newStoredAge;
                r->lastRefreshTime = std::chrono::steady_clock::now();
                r->flags = out.flags;
                out.record = r;
            }
        }
        else if (area.lsdb.touchRefresh(ctx.key))
        {
            if (auto* r = area.lsdb.find(ctx.key))
            {
                r->flags = out.flags;
                out.record = r;
            }
        }
        return out;
    }

    return out;
}

void Area::Private::installLsa(Result& result, const IncomingLsaContext& ctx, LsaBody& body)
{
    if (result.decision.newLsa && !onNewLsa())
        if (!ctx.selfOriginatedKey) return;

    if (result.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = area.lsdb.upsertMeta(ctx, result.flags);
        rec.body = std::move(body);
        result.record = &rec;
        result.decision.shouldFightBack = true;
    }
    else if (result.decision.action == InstallAction::FLUSH_MAX_AGE ||
        result.decision.shouldStoreReplace)
    {
        LsaRecord& rec = area.lsdb.upsertMeta(ctx, result.flags);
        rec.body = std::move(body);
        result.record = &rec;
    }
}

void Area::Private::installLsa(Result& result, const IncomingLsaContext& ctx, const LsaBody& body)
{
    if (result.decision.newLsa && !onNewLsa())
        if (!ctx.selfOriginatedKey) return;

    if (result.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = area.lsdb.upsertMeta(ctx, result.flags);
        rec.body = body;
        result.record = &rec;
        result.decision.shouldFightBack = true;
    }
    else if (result.decision.action == InstallAction::FLUSH_MAX_AGE ||
        result.decision.shouldStoreReplace)
    {
        LsaRecord& rec = area.lsdb.upsertMeta(ctx, result.flags);
        rec.body = body;
        result.record = &rec;
    }
}

void Area::Private::startAgingTimer()
{
    agingTimerId = 0;
    auto nextFire = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    agingTimerId = area.scheduler.postAfter(nextFire, [this](uint32_t) {
        agingTimerId = 0;
        onAgingTick();
    });
}

void Area::Private::onAgingTick()
{
    // Increment all LSA ages by 1 second, find how many hit MaxAge
    size_t expired = area.lsdb.ageAll(1, OSPF_MAX_AGE, false);

    if (expired > 0)
    {
        // Collect MaxAge LSAs, flood them, then purge
        std::vector<std::pair<LsaKey, LsaRecord*>> maxAgeLsas;
        for (auto& [key, record] : area.lsdb.getIterableLSDB())
        {
            if (record.header.age >= OSPF_MAX_AGE)
                maxAgeLsas.push_back({key, &record});
        }

        for (auto& [key, record] : maxAgeLsas)
        {
            FloodInfo info{FloodReason::FLUSH};
            area.floodMgr.enqueueFlood(LsaRecordRef{key, *record}, info);
        }

        area.lsdb.purgeExpired(OSPF_MAX_AGE);

        area.spfMgr.requestSpf();
    }

    startAgingTimer();
}

bool Area::Private::onNewLsa()
{
    auto maxLsa = area.process.configs.get<config::Ospf::MAX_LSA>();
    if (!maxLsa.hasValue())
        return true;

    float maxThresholdPercent = static_cast<float>(area.process.configs.get<config::Ospf::MAX_LSA_THRESHOLD>().load() / 100.0f) ;
    uint32_t maxThreshold = static_cast<uint32_t>(maxThresholdPercent * static_cast<float>(maxLsa.load()));
    if (maxThreshold <= area.lsdb.size())
    {
        // TODO: warning
    }

    if (maxLsa.load() <= area.lsdb.size())
    {
        ignoreLsa();
        return false;
    }
    return true;
}

void Area::Private::ignoreLsa()
{
    ignoreSize++;

    // Ignore count
    uint32_t maxSize = area.process.configs.get<config::Ospf::MAX_LSA_IGNORE_COUNT>().load();
    if (ignoreSize >= maxSize)
        area.process.enqueueReset();

    // Ignore timer
    startIgnoreTimer();
}

void Area::Private::startIgnoreTimer()
{
    if (ignoreTid != 0) return;
    uint16_t timeout = area.process.configs.get<config::Ospf::MAX_LSA_IGNORE_TIME>().load();
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::minutes(timeout);
    ignoreTid = area.scheduler.postAfter(expirationTime, [this](uint32_t)
    {
        startResetTimer();
    });
}

void Area::Private::startResetTimer()
{
    if (resetTid != 0) return;
    uint16_t timeout = area.process.configs.get<config::Ospf::MAX_LSA_RESET_TIME>().load();
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::minutes(timeout);
    resetTid = area.scheduler.postAfter(expirationTime, [this](uint32_t)
    {
        area.process.enqueueReset();
    });
}

InstallResult Area::Private::evaluateIncomingLsa(const LsaRecord* existing, IncomingLsaContext& ctx, const LsaBody& body)
{
    InstallResult out{};

    // Determine which LSA types affect the SPF graph topology
    if (area.process.isV3)
    {
        const uint16_t t = ctx.key.lsaType;
        out.affectsSpfGraph = (t == OSPFV3_LSA_ROUTER ||
                               t == OSPFV3_LSA_NETWORK ||
                               t == OSPFV3_LSA_INTER_AREA_PREFIX ||
                               t == OSPFV3_LSA_INTER_AREA_ROUTER ||
                               t == OSPFV3_LSA_INTRA_AREA_PREFIX ||
                               t == OSPFV3_LSA_LINK);
    }
    else
    {
        // V2: Router(1), Network(2), SummaryNet(3), SummaryASBR(4)
        out.affectsSpfGraph = ctx.key.lsaType >= 1 && ctx.key.lsaType <= 4;
    }

    if (!ctx.checksumValid)
    {
        out.shouldAck = false;
        out.action = InstallAction::REJECT_INVALID;
        return out;
    }

    // Missing -> install unless it is a MaxAge flush with no existing entry
    if (!existing)
    {
        out.newLsa = true;

        // Missing + MaxAge -> flood and ACK (RFC 2328 §13 step 4)
        if (isMaxAge(ctx.header, OSPF_MAX_AGE))
        {
            out.action = InstallAction::FLUSH_MAX_AGE;
            out.shouldFlood = true;
            out.shouldStoreReplace = true;
            return out;
        }

        out.action = InstallAction::INSTALL_NEWER;
        out.shouldStoreReplace = true;
        out.shouldFlood = true;

        if (out.affectsSpfGraph)
            out.topologyChanged = true;

        return out;
    }

    out.compare = compareLsaHeaders(ctx.header, existing->header);

    switch (out.compare)
    {
        case LsaCompareResult::OLDER:
            out.action = InstallAction::IGNORE_OLDER;
            return out;
        case LsaCompareResult::SAME:
            out.action = InstallAction::IGNORE_DUPLICATE;
            return out;
        case LsaCompareResult::NEWER:
        {
            // MinLSArrival check (RFC 2328 §13 step 5b) — rate-limit acceptance
            auto minArrivalMs = area.process.configs.get<config::Ospf::LSA_ARRIVAL>().load();
            auto minArrival = existing->lastRefreshTime + std::chrono::milliseconds(minArrivalMs);
            if (std::chrono::steady_clock::now() < minArrival)
            {
                out.action = InstallAction::IGNORE_DUPLICATE;
                out.shouldAck = true;
                out.shouldFlood = false;
                return out;
            }

            if (ctx.header.age == OSPF_MAX_AGE)
            {
                out.action = InstallAction::FLUSH_MAX_AGE;
                out.shouldStoreReplace = true;
                out.shouldFlood = true;

                if (out.affectsSpfGraph)
                    out.topologyChanged = true;

                return out;
            }

            if (ctx.selfOriginatedKey &&
                ctx.header.sequence >= existing->header.sequence)
            {
                out.action = InstallAction::FIGHT_BACK_SELF;
                out.shouldFightBack = true;
                out.shouldFlood = true;
                return out;
            }

            out.action = InstallAction::INSTALL_NEWER;
            out.shouldStoreReplace = true;
            out.shouldFlood = true;

            if (out.affectsSpfGraph)
            {
                if (!compareLsaBody(existing->body, body))
                    out.topologyChanged = true;
                else
                    ctx.info.reason = FloodReason::REFRESH;
            }

            return out;
        }
    }
}

LsaCompareResult Area::Private::compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const
{
    if (a.sequence != b.sequence)
        return (a.sequence > b.sequence) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    if (a.checksum != b.checksum)
        return (a.checksum > b.checksum) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    const bool aMax = isMaxAge(a, OSPF_MAX_AGE);
    const bool bMax = isMaxAge(b, OSPF_MAX_AGE);

    if (aMax != bMax)
        return aMax ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    const uint16_t diff = absDiffU16(a.age, b.age);
    if (diff > OSPF_MAX_DIFF)
        return (a.age < b.age) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    return LsaCompareResult::SAME;
}

bool Area::Private::compareLsaBody(const LsaBody& a, const LsaBody& b)
{
    if (a.index() != b.index())
        return true;

    return std::visit(
        [](const auto& lhs, const auto& rhs) -> bool
        {
            if constexpr (requires { lhs == rhs; })
                return lhs == rhs;
            else
                return true;
        },
        a, b
    );
}

template std::optional<Area::Result> Area::processLsa<PolicyV2>(IncomingLsaContext&, LsaBody&);
template std::optional<Area::Result> Area::processLsa<PolicyV3>(IncomingLsaContext&, LsaBody&);

template std::optional<Area::Result> Area::processLsa<PolicyV2>(IncomingLsaContext&, const LsaBody&);
template std::optional<Area::Result> Area::processLsa<PolicyV3>(IncomingLsaContext&, const LsaBody&);

template void Area::Private::postProcess<PolicyV2>(Result&, IncomingLsaContext&, const LsaBody&);
template void Area::Private::postProcess<PolicyV3>(Result&, IncomingLsaContext&, const LsaBody&);

template bool Area::Private::preProcess<PolicyV2>(IncomingLsaContext&, const LsaBody&);
template bool Area::Private::preProcess<PolicyV3>(IncomingLsaContext&, const LsaBody&);

template void Area::processSummaries<PolicyV2>(std::unordered_map<LsaKey, LsaBody>&);
template void Area::processSummaries<PolicyV3>(std::unordered_map<LsaKey, LsaBody>&);

template void Area::processExternalLsa<PolicyV2>(IncomingLsaContext&, const LsaBody&);
template void Area::processExternalLsa<PolicyV3>(IncomingLsaContext&, const LsaBody&);
} // namespace routing
