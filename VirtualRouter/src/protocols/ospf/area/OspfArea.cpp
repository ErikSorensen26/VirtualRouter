// OspfArea.cpp

#include "OspfArea.h"
#include <OspfInterface.h>
#include <OspfProcess.h>
#include <OspfTypes.hpp>
#include <OspfNeighbor.h>
#include <OspfTypes.hpp>
#include <OspfFletcher.hpp>
#include <Interface.h>
#include <InterfaceType.hpp>
#include <OspfNeighborTable.h>
#include <OspfNeighbor.h>
#include <variant>
#include <OspfRouteManager.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <limits>

#include <OspfRegistry.hpp>

#include "OspfOriginator.h"
#include <OspfOriginatorV2.h>
#include <OspfOriginatorV3.h>

namespace OSPF
{
OspfArea::OspfArea(OspfProcess& base, uint32_t id, std::pmr::memory_resource* mr)
    : mr(mr ? mr : std::pmr::get_default_resource()),
      configs(base.getConfigs().get<Config::Ospf::AREA_CONFIGS>().getMutable().emplace_back(
          areaId, base.routingInstance->global.registry.create<Config::OspfAreaRegistry>(
              Config::generateOspfAreaKey(base.getConfigKey(), id))).second),
      db(mr),
      fq(0),
      base(base),
      spfMgr(*this, base.tmgr),
      flags(base.isV3),
      originator(base.isV3
          ? *static_cast<OspfOriginator*>(new OspfOriginatorV3(*this))
          : *static_cast<OspfOriginator*>(new OspfOriginatorV2(*this))
      ),
      type(configs->get<Config::OspfArea::AREA_TYPE>().load()),
      areaId(id)
{}

void OspfArea::clear()
{
    db.clear();
}

void OspfArea::releaseMemory()
{
    db.releaseMemory();
}

void OspfArea::runDCIntegrityScan()
{
    bool enabled = db.runDCIntegrityScan();
    bool old = dcCompatible.exchange(enabled, std::memory_order_acq_rel);
    if (old != enabled)
    {
        auto& ifaceMgr = base.getIfaceMgr();
        std::shared_lock<std::shared_mutex> lk(ifaceMgr.interfaceMutex);
        for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
        {
            auto& ifaceConfigs = iface.getConfigs();
            if ((ifaceConfigs.get<Config::OspfInterface::DEMAND_CIRCUIT>().load() ||
                ifaceConfigs.get<Config::OspfInterface::FLOOD_REDUCTION>().load()) &&
                iface.floodReduction.load() != enabled)
            {
                iface.floodReduction.store(enabled, std::memory_order_release);

                // TODO: refresh interface
            }
        }
    }
}

void OspfArea::setFloodReduction(OspfInterface& iface)
{
    auto& ifaceConfigs = iface.getConfigs();
    const bool enableFloodReduction =
        dcCompatible.load(std::memory_order_relaxed) && (
            ifaceConfigs.get<Config::OspfInterface::FLOOD_REDUCTION>().load() ||
            ifaceConfigs.get<Config::OspfInterface::DEMAND_CIRCUIT>().load()
        );

    if (iface.floodReduction.load(std::memory_order_relaxed) != enableFloodReduction)
    {
        iface.floodReduction.store(enableFloodReduction, std::memory_order_relaxed);
        // TODO: refresh interface
    }
}

bool OspfArea::isValidForwardAddress(const IPAddress& addr) const
{
    if ((type == AreaType::NSSA || type == AreaType::TOTALLY_NSSA) &&
        configs->get<Config::OspfArea::NSSA_SUPPRESS_FA>().load())
        return false;

    return base.getRib().lpmLookup(addr, areaId);
}

void OspfArea::syncRangeConfig()
{
    auto& cfgRanges = configs->get<Config::OspfArea::RANGE>();
    std::unordered_set<IPPrefix> activeRanges;

    {
        std::lock_guard<std::mutex> lk(rangeMu);

        std::unordered_set<IPPrefix> seen;

        cfgRanges.withRead([&](const std::tuple<IPPrefix, bool, std::optional<uint32_t>>& t)
        {
            const auto& [pfx, noAdv, cost] = t;
            seen.insert(pfx);

            auto& r = ranges[pfx];
            r.notAdvertise = noAdv;
            r.costOverride = cost;
        });

        for (auto it = ranges.begin(); it != ranges.end();)
        {
            if (seen.find(it->first) == seen.end())
                it = ranges.erase(it);
            else
                ++it;
        }

        for (const auto& [r, _] : ranges)
            activeRanges.insert(r);
    }

    syncRangeSuppression(activeRanges);

    base.isV3 ? flood<PolicyV3>() : flood<PolicyV2>();
}

void OspfArea::syncRangeSuppression(const std::unordered_set<IPPrefix>& activeRanges, bool abrChange)
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

void OspfArea::syncRangeRuntime(const std::vector<std::pair<IPPrefix, OspfPath>>& intraRoutes, bool abrChange)
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

        std::lock_guard<std::mutex> lk(rangeMu);

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
        std::lock_guard<std::mutex> lk(rangeMu);

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
                    : readU32(pfx.addr);

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

std::unordered_set<IPPrefix> OspfArea::getRanges()
{
    std::unordered_set<IPPrefix> rs;
    std::lock_guard<std::mutex> lk(rangeMu);
    for (const auto& [pfx, _] : ranges)
        rs.insert(pfx);
    return rs;
}

std::unordered_map<IPPrefix, std::pair<uint32_t, uint32_t>> OspfArea::computeRangeContributors(
    const std::vector<std::pair<IPPrefix, OspfPath>>& intraAreaRoutes,
    const std::unordered_map<IPPrefix, OspfAreaRange>& activeRanges)
{
    std::unordered_map<IPPrefix, std::pair<uint32_t, uint32_t>> rcs;

    rcs.reserve(activeRanges.size());
    for (const auto& [pfx, _] : activeRanges)
        rcs.emplace(pfx, 0, std::numeric_limits<uint32_t>::max());

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

template<typename Policy>
void OspfArea::flood()
{
    auto floodList = fq.tryDequeueBatch();
    auto& ifaceMgr = base.getIfaceMgr();
    std::shared_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);
    for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
    {
        if (id.area == areaId && !iface.getConfigs().get<Config::OspfInterface::DATABASE_FILTER>().load())
            send(iface, floodList);
    }

    if (shouldRequestSpf.load(std::memory_order_relaxed))
        spfMgr.requestSpf<Policy>();
}

void OspfArea::send(OspfInterface& iface, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    auto& ntable = iface.getNTable();
    auto& dispatcher = iface.getDispatcher();

    if (iface.getConfigs().get<Config::OspfInterface::NETWORK>().load() == NetworkType::BROADCAST)
    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        dispatcher.sendLSUpdate(nullptr, records);
    }
    else
    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            dispatcher.sendLSUpdate(&nbr, records);
        }
    }
}

template <typename Policy>
std::optional<OspfArea::Result> OspfArea::processLsa(IncomingLsaContext& ctx, LsaBody& body)
{
    bool isNssa = type == AreaType::NSSA || type == AreaType::TOTALLY_NSSA;
    if (std::holds_alternative<typename Policy::InterNetworkLsa>(body) &&
        (type == AreaType::TOTALLY_STUB || type == AreaType::TOTALLY_NSSA))
        return std::nullopt;
    if (std::holds_alternative<typename Policy::InterRouterLsa>(body) && type != AreaType::NORMAL)
        return std::nullopt;
    if (std::holds_alternative<typename Policy::ExternalLsa>(body))
    {
        //bool isRouteNssa = base.isNssaExternal<Policy>(ctx.header, std::get<typename Policy::ExternalLsa>(body));
        bool isRouteNssa = ctx.key.lsaType == Policy::NssaType;
        if (type == AreaType::NORMAL)
        {
            if (isRouteNssa) return std::nullopt;
        }
        else if (isNssa)
        {
            if (!isRouteNssa) return std::nullopt;
        }
        else return std::nullopt;
    }

    auto result = process(ctx, body);

    if (result.decision.action != InstallAction::REJECT_INVALID &&
        result.decision.action != InstallAction::IGNORE_OLDER &&
        result.decision.topologyChanged)
    {
        if (std::holds_alternative<typename Policy::ExternalLsa>(body))
        {
            base.distributeExternalLsa<Policy>(*this, ctx, body);
        }
        else if (std::holds_alternative<typename Policy::InterNetworkLsa>(body))
        {
            auto res = RouteManager::deriveInterAreaNetwork<Policy>(*this, ctx.key, ctx.header, body);
            base.getRib().replaceRoute(areaId, res);
        }
        else if (std::holds_alternative<typename Policy::InterRouterLsa>(body))
        {
            RouteManager::deriveInterAreaRouter<Policy>(*this, ctx.key, ctx.header, body);
        }
    }

    return result;
}

OspfArea::Result OspfArea::process(IncomingLsaContext& ctx, LsaBody& body)
{
    Result out{};

    const LsaRecord* existing = db.find(ctx.key);

    out.decision = evaluateIncomingLsa(existing, ctx, body);
    
    if (out.decision.action == InstallAction::REJECT_INVALID ||
        out.decision.action == InstallAction::IGNORE_OLDER)
    {
        return out;
    }

    const LsaRecordFlags lsaFlags = makeFlags(ctx);

    if (out.decision.action == InstallAction::IGNORE_DUPLICATE)
    {
        if (out.decision.shouldUpdateAgeOnly)
        {
            if (auto* r = db.find(ctx.key))
            {
                r->header.age = out.decision.newStoredAge;
                r->lastRefreshTime = std::chrono::steady_clock::now();
                r->flags = lsaFlags;
                out.record = r;
            }
        }
        else if (db.touchRefresh(ctx.key))
        {
            if (auto* r = db.find(ctx.key))
            {
                r->flags = lsaFlags;
                out.record = r;
            }
        }
        return out;
    }

    if (out.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = db.upsertMeta(ctx, lsaFlags);
        rec.body = body;
        out.record = &rec;
        out.decision.shouldFightBack = true;
    }
    else if (out.decision.action == InstallAction::FLUSH_MAX_AGE ||
        out.decision.shouldStoreReplace)
    {
        LsaRecord& rec = db.upsertMeta(ctx, lsaFlags);
        rec.body = body;
        out.record = &rec;
    }

    evaluateDecision(out, ctx);

    return out;
}

template <typename Policy>
void OspfArea::processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries)
{
    db.forEachInType(Policy::InterNetworkType, [&](LsaKey& key, LsaRecord& record) {
        if (!summaries.contains(key))
            originator.processReoriginatedLsa<Policy>(key, record.body, false, true);
    });

    for (auto& [key, body] : summaries)
    {
        originator.processReoriginatedLsa<Policy>(key, body);
    }
}

void OspfArea::processExternalLsa(IncomingLsaContext& ctx, LsaBody& body)
{
    bool expire = ctx.header.age == OSPF_MAX_AGE;

    // Manage type 4 if needed
    if (type == AreaType::NORMAL)
        originator.addExternal(ctx.key.advertisingRouter, ctx.key.linkStateId, expire);
    process(ctx, body);
}

void OspfArea::evaluateDecision(Result& result, const IncomingLsaContext& ctx)
{
    if (result.decision.shouldFlood && result.record)
        enqueueFlood(LsaRecordRef{ctx.key, *result.record}, ctx.info);
    if (result.decision.affectsSpfGraph && result.decision.topologyChanged && !shouldRequestSpf.load(std::memory_order_relaxed))
        shouldRequestSpf.store(true, std::memory_order_release);
}

bool OspfArea::compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const
{
    const LsaRecord* existing = db.find(key);

    // Check if LSA is missing
    if (!existing)
    {
        return false;
    }

    const LsaCompareResult cmp = compareLsaHeaders(hdr, existing->header);
    if (cmp == LsaCompareResult::NEWER)
    {
        return false;
    }

    return true;
}

LsaRecordFlags OspfArea::makeFlags(const IncomingLsaContext& ctx) noexcept
{
    LsaRecordFlags f = LsaRecordFlags::NONE;
    if (ctx.selfOriginatedKey) f |= LsaRecordFlags::SELF_ORIGINATED;
    if (ctx.checksumValid) f |= LsaRecordFlags::CHECKSUM_VALID;
    return f;
}

void OspfArea::enqueueFlood(LsaRecordRef& record, FloodInfo info)
{
    fq.enqueue(record, info);
}

void OspfArea::enqueueFlood(LsaRecordRef&& record, FloodInfo info)
{
    fq.enqueue(record, info);
}

InstallResult OspfArea::evaluateIncomingLsa(const LsaRecord* existing, IncomingLsaContext& ctx, const LsaBody& body)
{
    InstallResult out{};

    // RouterLsa, NetworkLsa, SummaryLsa, AsbrLsa
    out.affectsSpfGraph = ctx.key.lsaType >= 1 && ctx.key.lsaType <= 4;
        
    if (!ctx.checksumValid)
    {
        out.shouldAck = false;
        out.action = InstallAction::REJECT_INVALID;
        return out;
    }

    // Missing -> accept unless it is a pur flush
    if (!existing)
    {
        // Missing + MaxAge -> ACK only (not a flush)
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

LsaCompareResult OspfArea::compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const
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

bool OspfArea::compareLsaBody(const LsaBody& a, const LsaBody& b)
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

template std::optional<OspfArea::Result> OspfArea::processLsa<PolicyV2>(IncomingLsaContext&, LsaBody&);
template std::optional<OspfArea::Result> OspfArea::processLsa<PolicyV3>(IncomingLsaContext&, LsaBody&);

template void OspfArea::processSummaries<PolicyV2>(std::unordered_map<LsaKey, LsaBody>&);
template void OspfArea::processSummaries<PolicyV3>(std::unordered_map<LsaKey, LsaBody>&);

template void OspfArea::flood<PolicyV2>();
template void OspfArea::flood<PolicyV3>();
}
