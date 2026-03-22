// Area.cpp

#include <variant>
#include <limits>
#include <Global.h>
#include <VirtualRouter.h>

#include "Area.h"
#include "ospf/OspfTypes.hpp"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/topology/RouteManager.h"
#include "configs/registry/router/OspfRegistry.h"
#include "ospf/transmission/PacketDispatcher.h"
#include "ospf/ospfv2/area/OriginatorV2.h"
#include "ospf/ospfv3/area/OriginatorV3.h"

namespace OSPF
{
Area::Area(OspfProcess& base, uint32_t id, std::pmr::memory_resource* mr)
    : mr(mr ? mr : std::pmr::get_default_resource()),
      configs(base.routingInstance->getRegistry().emplaceBack(
          base.getConfigs().get<Config::Ospf::AREA_CONFIGS>(),
          id)
      ),
      db(mr),
      base(base),
      spfMgr(*this),
      flags(base.isV3),
      floodMgr(*this),
      originator(base.isV3
          ? *static_cast<Originator*>(new OriginatorV3(*this))
          : *static_cast<Originator*>(new OriginatorV2(*this))
      ),
      scheduler(base.getScheduler()),
      type(configs->get<Config::OspfArea::AREA_TYPE>().load()),
      areaId(id)
{
    configs->context().set(this);
    startAgingTimer();
}

Area::~Area()
{
    if (ignoreTid != 0)
        base.getScheduler().cancel(ignoreTid);
    if (resetTid != 0)
        base.getScheduler().cancel(resetTid);
    if (agingTimerId != 0)
        base.getScheduler().cancel(agingTimerId);
    base.getConfigs().get<Config::Ospf::AREA_CONFIGS>().erase(areaId);
}

void Area::initializeReset()
{
    scheduler.post([this] {
        reset();
    });
}

void Area::reset()
{
    // Reset all neighbors on all interfaces in this area
    auto& ifaceMgr = base.getIfaceMgr();
    for (auto& [ifId, iface] : ifaceMgr.ospfInterfaceList)
    {
        if (iface.getAreaId() != areaId) continue;
        for (auto& [rid, nbr] : iface.getNTable().neighbors)
            nbr.setState(Neighbor::State::DOWN);
    }

    // Flush and clear LSDB
    // MaxAge-flood all LSAs so neighbors know we're resetting
    for (auto& [key, record] : db.getIterableLSDB())
    {
        record.header.age = OSPF_MAX_AGE;
        FloodInfo info{FloodReason::FLUSH};
        floodMgr.enqueueFlood(LsaRecordRef{key, record}, info);
    }
    db.clear();

    // Re-originate all self-originated LSAs
    originator.fullRefresh();
}

void Area::startAgingTimer()
{
    agingTimerId = 0;
    auto nextFire = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    agingTimerId = scheduler.postAfter(nextFire, [this](uint32_t) {
        agingTimerId = 0;
        onAgingTick();
    });
}

void Area::onAgingTick()
{
    // Increment all LSA ages by 1 second, find how many hit MaxAge
    size_t expired = db.ageAll(1, OSPF_MAX_AGE, false);

    if (expired > 0)
    {
        // Collect MaxAge LSAs, flood them, then purge
        std::vector<std::pair<LsaKey, LsaRecord*>> maxAgeLsas;
        for (auto& [key, record] : db.getIterableLSDB())
        {
            if (record.header.age >= OSPF_MAX_AGE)
                maxAgeLsas.push_back({key, &record});
        }

        for (auto& [key, record] : maxAgeLsas)
        {
            FloodInfo info{FloodReason::FLUSH};
            floodMgr.enqueueFlood(LsaRecordRef{key, *record}, info);
        }

        db.purgeExpired(OSPF_MAX_AGE);

        if (base.isV3)
            spfMgr.requestSpf<PolicyV3>();
        else
            spfMgr.requestSpf<PolicyV2>();
    }

    startAgingTimer();
}

void Area::flushNeighborLsas(uint32_t neighborRid)
{
    // Collect all LSA keys originated by this neighbor
    std::vector<LsaKey> toFlush;
    db.forEach([&](const LsaKey& key, const LsaRecord&) {
        if (key.advertisingRouter == neighborRid)
            toFlush.push_back(key);
    });

    for (const auto& key : toFlush)
    {
        LsaRecord* record = db.find(key);
        if (!record) continue;

        record->header.age = OSPF_MAX_AGE;
        FloodInfo info{FloodReason::FLUSH};
        floodMgr.enqueueFlood(LsaRecordRef{key, *record}, info);
    }

    if (!toFlush.empty())
    {
        if (base.isV3)
            spfMgr.requestSpf<PolicyV3>();
        else
            spfMgr.requestSpf<PolicyV2>();
    }
}

void Area::clear()
{
    db.clear();
}

void Area::releaseMemory()
{
    db.releaseMemory();
}

void Area::runDCIntegrityScan()
{
    bool enabled = db.runDCIntegrityScan();
    bool old = dcCompatible.exchange(enabled, std::memory_order_acq_rel);
    if (old != enabled)
    {
        auto& ifaceMgr = base.getIfaceMgr();
        for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
        {
            auto& ifaceConfigs = iface.getConfigs();
            if ((ifaceConfigs.get<Config::OspfInterface::DEMAND_CIRCUIT>().load() ||
                ifaceConfigs.get<Config::OspfInterface::FLOOD_REDUCTION>().load()) &&
                iface.floodReduction != enabled)
            {
                iface.floodReduction = enabled;

                // TODO: refresh interface
            }
        }
    }
}

void Area::setFloodReduction(OspfInterface& iface)
{
    auto& ifaceConfigs = iface.getConfigs();
    const bool enableFloodReduction =
        dcCompatible.load(std::memory_order_relaxed) && (
            ifaceConfigs.get<Config::OspfInterface::FLOOD_REDUCTION>().load() ||
            ifaceConfigs.get<Config::OspfInterface::DEMAND_CIRCUIT>().load()
        );

    if (iface.floodReduction != enableFloodReduction)
    {
        iface.floodReduction = enableFloodReduction;
        // TODO: refresh interface
    }
}

bool Area::isValidForwardAddress(const IPAddress& addr) const
{
    if ((type == AreaType::NSSA || type == AreaType::TOTALLY_NSSA) &&
        configs->get<Config::OspfArea::NSSA_SUPPRESS_FA>().load())
        return false;

    if (base.getConfigs().get<Config::Ospf::LRC_FORWARDING_ADDRESS>().load())
    {
        return addr.isIPv6()
            ? base.routingInstance->getRib().lookup(addr.v6()) != nullptr
            : base.routingInstance->getRib().lookup(addr.v4()) != nullptr;
    }
    else
    {
        return base.getRib().lpmLookup(addr, areaId);
    }
}

void Area::syncRangeConfig()
{
    auto& cfgRanges = configs->get<Config::OspfArea::RANGE>();
    std::unordered_set<IPPrefix> activeRanges;
    rangePrefixes.clear();

    cfgRanges.withRead([&](const std::vector<std::tuple<IPPrefix, bool, std::optional<uint32_t>>>& ts)
    {
        for (const auto& t : ts)
        {
            const auto& [pfx, noAdv, cost] = t;
            rangePrefixes.insert(pfx);

            auto& r = ranges[pfx];
            r.notAdvertise = noAdv;
            r.costOverride = cost;
        }
    });

    for (auto it = ranges.begin(); it != ranges.end();)
    {
        if (rangePrefixes.find(it->first) == rangePrefixes.end())
        {
            it = ranges.erase(it);
            rangePrefixes.erase(it->first);
        }
        else
            ++it;
    }

    for (const auto& [r, _] : ranges)
        activeRanges.insert(r);

    syncRangeSuppression(activeRanges);
}

void Area::syncRangeSuppression(const std::unordered_set<IPPrefix>& activeRanges, bool abrChange)
{
    bool isABR = base.isABR();

    auto changes = isABR
        ? base.getRib().refreshIntraRangeSuppression(areaId, activeRanges)
        : base.getRib().refreshIntraRangeSuppression(areaId, {});

    if (base.isV3)
        base.reoriginateSummaries<PolicyV3>(*this, changes);
    else
        base.reoriginateSummaries<PolicyV2>(*this, changes);

    if (isABR || abrChange)
    {
        std::vector<std::pair<IPPrefix, OspfPath>> intra = isABR
            ? base.getRib().getIntraAreaRoutes(areaId)
            : std::vector<std::pair<IPPrefix, OspfPath>>{};

        syncRangeRuntime(intra, abrChange);
    }
}

void Area::syncRangeRuntime(const std::vector<std::pair<IPPrefix, OspfPath>>& intraRoutes, bool abrChange)
{
    struct SummaryAction
    {
        uint32_t lsid;
        IPPrefix pfx;
        uint32_t metric;
        bool flush;
    };
    struct DiscardAction
    {
        IPPrefix pfx;
        uint32_t metric;
        bool install;
    };

    std::vector<SummaryAction> summaryActions;
    std::vector<DiscardAction> discardActions;

    // If not ABR: withdraw any previously originated range summaries + discards, but do NOT just clear silently.
    if (!base.isABR())
    {
        if (!abrChange) return;

        // Build withdrawals from existing runtime state
        for (auto& [pfx, r] : ranges)
        {
            if (r.summary.has_value())
            {
                summaryActions.push_back({ r.summary.value(), pfx, 0, true });
                r.summary = std::nullopt;
            }

            if (r.discardPresent)
                discardActions.push_back({ pfx, 0, false });

            r.contributorCount = 0;
            r.computedMetric = 0;
        }
    }
    else
    {
        // ABR case: compute contributors and update runtime state
        auto rcs = computeRangeContributors(intraRoutes, ranges);

        for (auto& [pfx, r] : ranges)
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
                const uint32_t lsid = base.isV3
                    ? base.monotonicIntraId.fetch_add(1, std::memory_order_release)
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
            if (r.discardPresent && shouldDiscard)
                discardActions.push_back({ pfx, metric, true });
            else if (r.discardPresent && !shouldDiscard)
                discardActions.push_back({ pfx, 0, false });

            r.contributorCount = count;
            r.computedMetric = active ? metric : 0;
        }
    }

    // Execute actions OUTSIDE rangeMu
    for (const auto& a : summaryActions)
        originator.originateSummary(a.lsid, a.pfx, a.metric, a.flush);

    auto& rib = base.getRib();
    if (base.getConfigs().get<Config::Ospf::DISCARD_INTERNAL>().load())
    {
        for (const auto& d : discardActions)
        {
            if (d.install)
            {
                const uint8_t ad = base.getConfigs().get<Config::Ospf::DISCARD_INTERNAL_DISTANCE>().load();
                rib.installDiscardRoute({ d.pfx, areaId }, d.metric, ad);
            }
            else
            {
                rib.withdrawDiscardRoute({ d.pfx, areaId });
            }
        }
    }
}

const std::unordered_set<IPPrefix>& Area::getRanges() const
{
    return rangePrefixes;
}

std::unordered_map<IPPrefix, std::pair<uint32_t, uint32_t>> Area::computeRangeContributors(
    const std::vector<std::pair<IPPrefix, OspfPath>>& intraAreaRoutes,
    const std::unordered_map<IPPrefix, AreaRange>& activeRanges)
{
    std::unordered_map<IPPrefix, std::pair<uint32_t, uint32_t>> rcs;

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

void Area::send(OspfInterface& iface, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    auto& ntable = iface.getNTable();
    auto& dispatcher = iface.getDispatcher();

    if (iface.getConfigs().get<Config::OspfInterface::NETWORK>().load() == NetworkType::BROADCAST)
    {
        dispatcher.sendReliableLSUpdate(nullptr, records);
    }
    else
    {
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            dispatcher.sendReliableLSUpdate(&nbr, records);
        }
    }
}

template <typename Policy>
std::optional<Area::Result> Area::processLsa(IncomingLsaContext& ctx, LsaBody& body)
{
    if (!preProcess<Policy>(ctx, body)) return std::nullopt;
    auto result = process(ctx, body);

    installLsa(result, ctx, body);
    evaluateDecision<Policy>(result, ctx);

    postProcess<Policy>(result, ctx, body);

    return result;
}

template <typename Policy>
std::optional<Area::Result> Area::processLsa(IncomingLsaContext& ctx, const LsaBody& body)
{
    preProcess<Policy>(ctx, body);
    auto result = process(ctx, body);

    evaluateDecision<Policy>(result, ctx);

    postProcess<Policy>(result, ctx, body);

    return result;
}

template <typename Policy>
bool Area::preProcess(IncomingLsaContext& ctx, const LsaBody& body)
{
    bool isNssa = type == AreaType::NSSA || type == AreaType::TOTALLY_NSSA;
    if (std::holds_alternative<typename Policy::InterNetworkLsa>(body) &&
        (type == AreaType::TOTALLY_STUB || type == AreaType::TOTALLY_NSSA))
        return false;
    if (std::holds_alternative<typename Policy::InterRouterLsa>(body) && type != AreaType::NORMAL)
        return true;
    if (std::holds_alternative<typename Policy::ExternalLsa>(body))
    {
        //bool isRouteNssa = base.isNssaExternal<Policy>(ctx.header, std::get<typename Policy::ExternalLsa>(body));
        bool isRouteNssa = ctx.key.lsaType == Policy::NssaType;
        if (type == AreaType::NORMAL)
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
void Area::postProcess(Result& result, IncomingLsaContext& ctx, const LsaBody& body)
{
    if (result.decision.action != InstallAction::REJECT_INVALID &&
        result.decision.action != InstallAction::IGNORE_OLDER &&
        result.decision.topologyChanged)
    {
        // Router and Network LSAs carry the DC options bit; re-check DC compatibility
        if (std::holds_alternative<typename Policy::RouterLsa>(body) ||
            std::holds_alternative<typename Policy::NetworkLsa>(body))
        {
            runDCIntegrityScan();
        }

        if (std::holds_alternative<typename Policy::ExternalLsa>(body))
        {
            base.distributeExternalLsa<Policy>(*this, ctx, body);
        }
        else if (std::holds_alternative<typename Policy::InterNetworkLsa>(body))
        {
            auto res = RouteManager::deriveInterAreaNetwork<Policy>(*this, ctx.key, ctx.header, body);
            auto changes = base.getRib().replaceRoute(*this, res);
            if (!changes.empty()) base.reoriginateSummaries<Policy>(*this, changes);
        }
        else if (std::holds_alternative<typename Policy::InterRouterLsa>(body))
        {
            RouteManager::deriveInterAreaRouter<Policy>(*this, ctx.key, ctx.header, body);
        }
    }
}

Area::Result Area::process(IncomingLsaContext& ctx, const LsaBody& body)
{
    Result out{};

    const LsaRecord* existing = db.find(ctx.key);

    out.decision = evaluateIncomingLsa(existing, ctx, body);
    
    if (out.decision.action == InstallAction::REJECT_INVALID ||
        out.decision.action == InstallAction::IGNORE_OLDER)
    {
        return out;
    }

    out.flags = makeFlags(ctx);

    if (out.decision.action == InstallAction::IGNORE_DUPLICATE)
    {
        if (out.decision.shouldUpdateAgeOnly)
        {
            if (auto* r = db.find(ctx.key))
            {
                r->header.age = out.decision.newStoredAge;
                r->lastRefreshTime = std::chrono::steady_clock::now();
                r->flags = out.flags;
                out.record = r;
            }
        }
        else if (db.touchRefresh(ctx.key))
        {
            if (auto* r = db.find(ctx.key))
            {
                r->flags = out.flags;
                out.record = r;
            }
        }
        return out;
    }

    return out;
}

void Area::installLsa(Result& result, const IncomingLsaContext& ctx, LsaBody& body)
{
    if (result.decision.newLsa && !onNewLsa())
        if (!ctx.selfOriginatedKey) return;

    if (result.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = db.upsertMeta(ctx, result.flags);
        rec.body = std::move(body);
        result.record = &rec;
        result.decision.shouldFightBack = true;
    }
    else if (result.decision.action == InstallAction::FLUSH_MAX_AGE ||
        result.decision.shouldStoreReplace)
    {
        LsaRecord& rec = db.upsertMeta(ctx, result.flags);
        rec.body = std::move(body);
        result.record = &rec;
    }
}

void Area::installLsa(Result& result, const IncomingLsaContext& ctx, const LsaBody& body)
{
    if (result.decision.newLsa && !onNewLsa())
        if (!ctx.selfOriginatedKey) return;

    if (result.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = db.upsertMeta(ctx, result.flags);
        rec.body = body;
        result.record = &rec;
        result.decision.shouldFightBack = true;
    }
    else if (result.decision.action == InstallAction::FLUSH_MAX_AGE ||
        result.decision.shouldStoreReplace)
    {
        LsaRecord& rec = db.upsertMeta(ctx, result.flags);
        rec.body = body;
        result.record = &rec;
    }
}

bool Area::onNewLsa()
{
    auto& processConfigs = base.getConfigs();

    auto& maxLsa = processConfigs.get<Config::Ospf::MAX_LSA>();
    if (!maxLsa.hasValue())
        return true;

    float maxThresholdPercent = static_cast<float>(processConfigs.get<Config::Ospf::MAX_LSA_THRESHOLD>().load() / 100.0f) ;
    uint32_t maxThreshold = static_cast<uint32_t>(maxThresholdPercent * static_cast<float>(maxLsa.load()));
    if (maxThreshold <= db.size())
    {
        // TODO: warning
    }

    if (maxLsa.load() <= db.size())
    {
        ignoreLsa();
        return false;
    }
    return true;
}

void Area::ignoreLsa()
{
    auto& processConfigs = base.getConfigs();
    ignoreSize++;

    // Ignore count
    uint32_t maxSize = processConfigs.get<Config::Ospf::MAX_LSA_IGNORE_COUNT>().load();
    if (ignoreSize >= maxSize)
        base.initiateReset();

    // Ignore timer
    startIgnoreTimer();
}

void Area::startIgnoreTimer()
{
    if (ignoreTid != 0) return;
    uint16_t timeout = base.getConfigs().get<Config::Ospf::MAX_LSA_IGNORE_TIME>().load();
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::minutes(timeout);
    ignoreTid = scheduler.postAfter(expirationTime, [this](uint32_t)
    {
        startResetTimer();
    });
}

void Area::startResetTimer()
{
    if (resetTid != 0) return;
    uint16_t timeout = base.getConfigs().get<Config::Ospf::MAX_LSA_RESET_TIME>().load();
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::minutes(timeout);
    resetTid = scheduler.postAfter(expirationTime, [this](uint32_t)
    {
        base.initiateReset();
    });
}

template <typename Policy>
void Area::processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries)
{
    db.forEachInType(Policy::InterNetworkType, [&](const LsaKey& key, const LsaRecord& record) {
        if (!summaries.contains(key))
            originator.originateLsa<Policy>(key, record.body, true);
    });

    for (auto& [key, body] : summaries)
    {
        originator.originateLsa<Policy>(key, body, false);
    }
}

template <typename Policy>
void Area::processExternalLsa(IncomingLsaContext& ctx, const LsaBody& body)
{
    if (!preProcess<Policy>(ctx, body)) return;

    bool expire = ctx.header.age == OSPF_MAX_AGE;
    LsaBody bodyCopy = body;

    auto& external = std::get<typename Policy::ExternalLsa>(bodyCopy);
    if constexpr (std::is_same_v<Policy, PolicyV2>)
    {
        if (external.forwardingAddress != 0 && !isValidForwardAddress(IPAddress(external.forwardingAddress)))
            external.forwardingAddress = 0;
    }
    else
    {
        if (external.forwardingAddress.has_value() && !isValidForwardAddress(external.forwardingAddress.value()))
            external.forwardingAddress = std::nullopt;
    }

    // Manage type 4 if needed
    if (type == AreaType::NORMAL)
        originator.addExternal(ctx.key.advertisingRouter, ctx.key.linkStateId, expire);

    auto result = process(ctx, body);
    installLsa(result, ctx, body);
    evaluateDecision<Policy>(result, ctx);
}

template <typename Policy>
void Area::evaluateDecision(Result& result, const IncomingLsaContext& ctx)
{
    if (result.decision.shouldFlood && result.record)
        floodMgr.enqueueFlood(LsaRecordRef{ctx.key, *result.record}, ctx.info);
    if (result.decision.affectsSpfGraph && result.decision.topologyChanged)
        spfMgr.requestSpf<Policy>();
}

bool Area::compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const
{
    const LsaRecord* existing = db.find(key);

    // We don't have this LSA at all — need to request it
    if (!existing)
        return true;

    // Neighbor has a newer version — need to request it
    const LsaCompareResult cmp = compareLsaHeaders(hdr, existing->header);
    return cmp == LsaCompareResult::NEWER;
}

LsaRecordFlags Area::makeFlags(const IncomingLsaContext& ctx) noexcept
{
    LsaRecordFlags f = LsaRecordFlags::NONE;
    if (ctx.selfOriginatedKey) f |= LsaRecordFlags::SELF_ORIGINATED;
    if (ctx.checksumValid) f |= LsaRecordFlags::CHECKSUM_VALID;
    return f;
}

InstallResult Area::evaluateIncomingLsa(const LsaRecord* existing, IncomingLsaContext& ctx, const LsaBody& body)
{
    InstallResult out{};

    // Determine which LSA types affect the SPF graph topology
    if (base.isV3)
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
            auto minArrivalMs = base.getConfigs().get<Config::Ospf::LSA_ARRIVAL>().load();
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

LsaCompareResult Area::compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const
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

bool Area::compareLsaBody(const LsaBody& a, const LsaBody& b)
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

template void Area::evaluateDecision<PolicyV2>(Result&, const IncomingLsaContext&);
template void Area::evaluateDecision<PolicyV3>(Result&, const IncomingLsaContext&);

template void Area::postProcess<PolicyV2>(Result&, IncomingLsaContext&, const LsaBody&);
template void Area::postProcess<PolicyV3>(Result&, IncomingLsaContext&, const LsaBody&);

template bool Area::preProcess<PolicyV2>(IncomingLsaContext&, const LsaBody&);
template bool Area::preProcess<PolicyV3>(IncomingLsaContext&, const LsaBody&);

template void Area::processSummaries<PolicyV2>(std::unordered_map<LsaKey, LsaBody>&);
template void Area::processSummaries<PolicyV3>(std::unordered_map<LsaKey, LsaBody>&);

template void Area::processExternalLsa<PolicyV2>(IncomingLsaContext&, const LsaBody&);
template void Area::processExternalLsa<PolicyV3>(IncomingLsaContext&, const LsaBody&);
}
