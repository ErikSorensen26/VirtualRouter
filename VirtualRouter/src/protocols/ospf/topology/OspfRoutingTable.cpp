// OspfRoutingTable.cpp

#include "OspfRoutingTable.h"

#include <OspfProcess.h>
#include <VirtualRouter.h>
#include <RoutingTable.hpp>

#include <algorithm>
#include <utility>

namespace OSPF
{
static RouteSource deriveOspfType(OspfRouteType type)
{
    switch (type)
    {
        case OspfRouteType::INTRA_AREA:
            return RouteSource::OSPF_INTRA;
        case OspfRouteType::INTER_AREA:
            return RouteSource::OSPF_INTER;
        case OspfRouteType::EXTERNAL:
            return RouteSource::OSPF_EXTERNAL;
        case OspfRouteType::NSSA:
            return RouteSource::OSPF_NSSA;
    }
    __builtin_unreachable();
}

static bool isIntraRangeSuppressed(const IPPrefix& prefix, const std::unordered_set<IPPrefix>& ranges)
{
    for (const auto& r : ranges)
        if (Functions::compareNetworkWithIp(r.addr, prefix.addr, r.prefixLength, prefix.af))
            return true;
    return false;
}

static bool isBetterPath(const OspfPath& a, const OspfPath& b)
{
    if (a.type != b.type) return a.type < b.type;
    if (a.cost != b.cost) return a.cost < b.cost;
    if (a.options != b.options) return a.options < b.options;
    return a.adminDistance < b.adminDistance;
}

static void addTrafficShare(std::vector<OspfNextHop>& nhs, uint8_t maxPaths, bool traffMin)
{
    if (nhs.size() <= 1)
        return;

    if (traffMin)
    {
        nhs.erase(std::unique(nhs.begin(), nhs.end(),
            [](const OspfNextHop& a, const OspfNextHop& b)
            {
                return a.interfaceId == b.interfaceId;
            }), nhs.end());
    }

    if (nhs.size() > maxPaths)
        nhs.resize(maxPaths);
}

static void dedupeNextHops(std::vector<OspfNextHop>& nhs)
{
    std::sort(nhs.begin(), nhs.end(),
        [](const OspfNextHop& x, const OspfNextHop& y)
        {
            if (x.interfaceId != y.interfaceId) return x.interfaceId < y.interfaceId;
            return x.nextHop < y.nextHop;
        });

    nhs.erase(std::unique(nhs.begin(), nhs.end()), nhs.end());
}

static std::vector<OspfNextHop> mergeEcmpNextHops(const std::vector<OspfPath>& bestSet)
{
    std::vector<OspfNextHop> merged;
    for (const auto& p : bestSet)
        merged.insert(merged.end(), p.nextHops.begin(), p.nextHops.end());

    dedupeNextHops(merged);
    return merged;
}

OspfRib::OspfRib(OspfProcess& p)
    : process(p), rib(process.routingInstance->routingTable) {}

const OspfRoute* OspfRib::lookup(const IPPrefix& prefix) const
{
    auto it = prefixStates.find(prefix);
    if (it == prefixStates.end() || !it->second.hasSelected)
        return nullptr;

    return &it->second.selected;
}

bool OspfRib::lpmLookup(const IPAddress& addr, uint32_t area) const
{
    auto it = areaIndex.find(area);
    if (it == areaIndex.end())
        return false;

    for (const IPPrefix& prefix : it->second)
    {
        if (prefix.contains(addr))
            return true;
    }
    return false;
}

std::vector<OspfRouteChange> OspfRib::replaceArea(OspfArea& area, const std::vector<std::pair<IPPrefix, OspfPath>>& paths)
{
    const uint32_t areaId = area.areaId;

    // Snapshot suppressed ranges ONCE (avoid per-prefix area locks)
    const std::unordered_set<IPPrefix> suppressedRanges = area.getRanges();

    std::unordered_set<IPPrefix> touched;
    touched.reserve(paths.size());

    // Snapshot the previously indexed prefixes for this area (if any).
    std::vector<IPPrefix> oldPrefixes;
    if (auto idxIt = areaIndex.find(areaId); idxIt != areaIndex.end())
    {
        oldPrefixes.reserve(idxIt->second.size());
        for (const auto& pfx : idxIt->second)
            oldPrefixes.push_back(pfx);
    }

    // Remove all non-discard candidates contributed by this area.
    for (const auto& pfx : oldPrefixes)
    {
        auto psIt = prefixStates.find(pfx);
        if (psIt == prefixStates.end())
            continue;

        auto& cand = psIt->second.canidates;

        cand.erase(
            std::remove_if(cand.begin(), cand.end(),
                [areaId](const OspfPath& p)
                {
                    return p.area.has_value() &&
                           p.area.value() == areaId &&
                           !p.discard;
                }),
            cand.end());

        touched.insert(pfx);
    }

    // Rebuild the index for this area from scratch.
    areaIndex[areaId].clear();

    // Add new candidates (stamp suppression) and index them.
    for (const auto& [pfx, inPath] : paths)
    {
        OspfPath path = inPath;

        // Range suppression applies ONLY to intra-area routes in this area.
        if (path.type == OspfRouteType::INTRA_AREA &&
            path.area.has_value() &&
            path.area.value() == areaId)
        {
            path.suppressed = isIntraRangeSuppressed(pfx, suppressedRanges);
        }

        prefixStates[pfx].canidates.push_back(std::move(path));
        areaIndex[areaId].insert(pfx);
        touched.insert(pfx);
    }

    // If this area now contributes nothing, drop the index entry.
    if (areaIndex[areaId].empty())
        areaIndex.erase(areaId);

    return recomputeLocked(touched, area.areaId, suppressedRanges);
}

std::vector<OspfRouteChange> OspfRib::replaceRoute(OspfArea& area, const std::pair<IPPrefix, std::optional<OspfPath>>& path)
{
    uint32_t areaId = area.areaId;

    auto idxIt = areaIndex.find(areaId);
    bool areaFound = idxIt != areaIndex.end();

    auto psIt = prefixStates.find(path.first);
    if (psIt != prefixStates.end())
    {
        auto& cand = psIt->second.canidates;

        cand.erase(
            std::remove_if(cand.begin(), cand.end(),
                [](const OspfPath& p)
                {
                    return !p.area.has_value() && !p.discard;
                }),
            cand.end());
        if (areaFound)
            idxIt->second.erase(path.first);
    }

    if (path.second.has_value())
    {
        OspfPath p = path.second.value();
        if (p.type == OspfRouteType::INTER_AREA && p.area.has_value() && p.area.value() == areaId)
        {
            const std::unordered_set<IPPrefix>& ranges = area.getRanges();
            p.suppressed = isIntraRangeSuppressed(path.first, ranges);
        }

        prefixStates[path.first].canidates.push_back(path.second.value());
        areaIndex[areaId].insert(path.first);
    }
    else
    {
        if (areaFound && idxIt->second.empty())
            areaIndex.erase(areaId);
    }

    RecomputeCtx ctx{
        .areaId = areaId,
        .ranges = area.getRanges()
    };

    recomputeLocked(path.first, process.getAF(), process.getProcId(), &ctx);

    std::vector<OspfRouteChange> changes;
    if (ctx.intraChanged) changes.push_back(ctx.intraChange);
    if (ctx.interChanged) changes.push_back(ctx.interChange);
    return changes;
}

void OspfRib::replaceExternals(const std::vector<std::pair<IPPrefix, OspfPath>>& paths)
{
    std::unordered_set<IPPrefix> touched;
    touched.reserve(paths.size());

    for (const auto& prefix : processWide)
    {
        auto psIt = prefixStates.find(prefix);
        if (psIt == prefixStates.end())
            continue;

        auto& cand = psIt->second.canidates;

        cand.erase(
            std::remove_if(cand.begin(), cand.end(),
                [](const OspfPath& p)
                {
                    return !p.area.has_value() && !p.discard;
                }),
            cand.end());
        touched.insert(prefix);
    }

    for (const auto& [prefix, path] : paths)
    {
        prefixStates[prefix].canidates.push_back(path);
        processWide.insert(prefix);
        touched.insert(prefix);
    }

    recomputeLocked(touched);
}

void OspfRib::replaceExternal(const std::pair<IPPrefix, std::optional<OspfPath>>& path)
{
    auto psIt = prefixStates.find(path.first);
    if (psIt != prefixStates.end())
    {
        auto& cand = psIt->second.canidates;

        cand.erase(
            std::remove_if(cand.begin(), cand.end(),
                [](const OspfPath& p)
                {
                    return !p.area.has_value() && !p.discard;
                }),
            cand.end());
        processWide.erase(path.first);
    }

    if (path.second.has_value())
    {
        prefixStates[path.first].canidates.push_back(path.second.value());
        processWide.insert(path.first);
    }

    recomputeLocked(path.first, process.getAF(), process.getProcId());
}

std::vector<std::pair<IPPrefix, OspfPath>> OspfRib::getIntraAreaRoutes(uint32_t area)
{
    std::vector<std::pair<IPPrefix, OspfPath>> areaRoutes;

    for (const auto& [prefix, state] : prefixStates)
    {
        if (!state.hasSelected)
            continue;

        const OspfPath* bestPath = nullptr;

        if (state.selected.type != OspfRouteType::INTRA_AREA)
            continue;
        if (!state.selected.area.has_value() || state.selected.area.value() != area)
            continue;
        if (state.selected.suppressed)
            continue;

        areaRoutes.push_back({prefix, *bestPath});
    }

    return areaRoutes;
}

void OspfRib::installDiscardRoute(const OspfDiscardKey& key, uint32_t cost, uint8_t ad)
{
    auto& state = prefixStates[key.prefix];

    discardRoutes.emplace(key, key.prefix);

    OspfRouteType type = key.areaId.has_value() ? OspfRouteType::INTER_AREA : OspfRouteType::EXTERNAL;
    auto sit = std::find_if(state.canidates.begin(), state.canidates.end(),
        [&](const OspfPath& p) { return p.area == key.areaId && p.type == type && p.discard; });

    if (sit != state.canidates.end())
    {
        sit->cost = cost;
        sit->adminDistance = ad;
    }
    else
    {
        state.canidates.push_back({
            .type = type,
            .area = key.areaId,
            .cost = cost,
            .adminDistance = ad,
            .options = 0,
            .discard = true,
            .nextHops = {}
        });
    }

    recomputeLocked(key.prefix, process.getAF(), process.getProcId());
}

void OspfRib::withdrawDiscardRoute(const OspfDiscardKey& key)
{
    if (auto it = prefixStates.find(key.prefix); it != prefixStates.end())
    {
        OspfRouteType type = key.areaId.has_value() ? OspfRouteType::INTER_AREA : OspfRouteType::EXTERNAL;
        auto& cand = it->second.canidates;
        cand.erase(
            std::remove_if(cand.begin(), cand.end(),
                [&key, &type](const OspfPath& p)
                {
                    return p.area == key.areaId && p.discard && p.type == type;
                }),
            cand.end());
    }

    discardRoutes.erase(key);
    recomputeLocked(key.prefix, process.getAF(), process.getProcId());
}

std::vector<OspfRouteChange> OspfRib::refreshIntraRangeSuppression(uint32_t areaId, const std::unordered_set<IPPrefix>& ranges)
{
    std::unordered_set<IPPrefix> touched;

    auto idxIt = areaIndex.find(areaId);
    if (idxIt == areaIndex.end())
        return {};

    // Iterate only prefixes that are known to have canidates from this area
    for (const auto& pfx : idxIt->second)
    {
        auto stIt = prefixStates.find(pfx);
        if (stIt == prefixStates.end())
            continue;

        auto& cand = stIt->second.canidates;

        bool hasIntraInThisArea = false;
        bool changed = false;

        const bool newSupp = isIntraRangeSuppressed(pfx, ranges);

        for (auto& path : cand)
        {
            if (path.type != OspfRouteType::INTRA_AREA)
                continue;
            if (!path.area.has_value() || path.area.value() != areaId)
                continue;

            hasIntraInThisArea = true;

            if (path.suppressed != newSupp)
            {
                path.suppressed = newSupp;
                changed = true;
            }
        }

        if (hasIntraInThisArea && changed)
            touched.insert(pfx);
    }

    if (touched.empty())
        return {};

    return recomputeLocked(touched, areaId, ranges);
}

bool OspfRib::recomputeLocked(const IPPrefix& prefix, AddressFamily af, uint32_t procId, RecomputeCtx* ctx)
{
    auto it = prefixStates.find(prefix);
    if (it == prefixStates.end())
        return false;

    auto& st = it->second;

    auto isIntraEffective = [&](const OspfRoute& r) -> bool
    {
        if (r.type != OspfRouteType::INTRA_AREA)
            return false;
        if (ctx && r.area.has_value() && r.area.value() == ctx->areaId)
            return !r.suppressed;
        return true;
    };

    auto isInterEffective = [&](const OspfRoute& r) -> bool
    {
        if (r.type != OspfRouteType::INTER_AREA)
            return false;
        if (ctx && r.area.has_value() && r.area.value() == ctx->areaId)
            return !r.suppressed;
        return true;
    };

    const bool hadOld = st.hasSelected;
    const OspfRoute oldCopy = hadOld ? st.selected : OspfRoute{};

    const bool oldIntraEffective = hadOld ? isIntraEffective(oldCopy) : false;
    const bool oldInterEffective = hadOld ? isInterEffective(oldCopy) : false;

    if (st.canidates.empty())
    {
        if (!hadOld)
        {
            prefixStates.erase(it);
            return false;
        }

        const RouteSource src = deriveOspfType(oldCopy.type);

        if (af == AddressFamily::IPv4)
            rib.removeEntry(readU32(prefix.addr), prefix.prefixLength, src, procId);
        else
            rib.removeEntry(readU128(prefix.addr), prefix.prefixLength, src, procId);

        st.hasSelected = false;
        prefixStates.erase(it);

        bool anyCtx = false;
        if (ctx && oldIntraEffective)
        {
            ctx->intraChange.prefix = oldCopy.prefix;
            ctx->intraChange.options = oldCopy.options;
            ctx->intraChange.cost = oldCopy.cost;
            ctx->intraChange.isRemoval = true;
            ctx->intraChanged = true;
            anyCtx = true;
        }

        if (oldInterEffective && ctx)
        {
            ctx->interChange.prefix = oldCopy.prefix;
            ctx->interChange.options = oldCopy.options;
            ctx->interChange.cost = oldCopy.cost;
            ctx->interChange.isRemoval = true;
            ctx->interChanged = true;
            anyCtx = true;
        }

        return anyCtx;
    }

    const OspfPath* best = nullptr;
    for (const auto& p : st.canidates)
        if (!best || isBetterPath(p, *best))
            best = &p;

    if (!best)
        return false;

    OspfRoute next{};
    next.prefix = prefix;
    next.type = best->type;
    next.cost = best->cost;
    next.area = best->area;
    next.adminDistance = best->adminDistance;
    next.options = best->options;
    next.suppressed = best->suppressed;

    next.paths.reserve(st.canidates.size());
    for (const auto& p : st.canidates)
    {
        if (*best == p)
            next.paths.push_back(p);
    }
#ifndef NDEBUG
    for (const auto& p : next.paths)
         assert(p.suppressed == next.suppressed);
#endif

    const bool newIntraEffective = isIntraEffective(next);
    const bool newInterEffective = isInterEffective(next);

    bool routeChanged = true;
    bool ecmpChanged = true;

    if (hadOld)
    {
        routeChanged = oldCopy != next;
        ecmpChanged = (oldCopy.paths != next.paths);
    }

    if (ctx)
    {
        if (oldIntraEffective != newIntraEffective)
        {
            ctx->intraChange.prefix = prefix;

            if (oldInterEffective && !newInterEffective)
            {
                ctx->intraChange.options = oldCopy.options;
                ctx->intraChange.cost = oldCopy.cost;
                ctx->intraChange.isRemoval = true;
            }
            else
            {
                ctx->intraChange.options = next.options;
                ctx->intraChange.cost = next.cost;
                ctx->intraChange.isRemoval = false;
            }

            ctx->intraChanged = true;
        }

        if (oldInterEffective != newInterEffective)
        {
            ctx->interChange.prefix = prefix;

            if (oldInterEffective && !newInterEffective)
            {
                ctx->interChange.options = oldCopy.options;
                ctx->interChange.cost = oldCopy.cost;
                ctx->interChange.isRemoval = true;
            }
            else
            {
                ctx->interChange.options = next.options;
                ctx->interChange.cost = next.cost;
                ctx->interChange.isRemoval = false;
            }

            ctx->interChanged = true;
        }
    }

    if (!hadOld || routeChanged || ecmpChanged)
    {
        if (hadOld)
        {
            const RouteSource oldSrc = deriveOspfType(oldCopy.type);
            if (af == AddressFamily::IPv4)
                rib.removeEntry(readU32(prefix.addr), prefix.prefixLength, oldSrc, procId);
            else
                rib.removeEntry(readU128(prefix.addr), prefix.prefixLength, oldSrc, procId);
        }
        
        auto merged = mergeEcmpNextHops(next.paths);
        addTrafficShare(
            merged,
            process.getConfigs().get<Config::Ospf::MAXIMUM_PATHS>().load(),
            process.getConfigs().get<Config::Ospf::TRAFFIC_SHARE_MIN>().load()
        );

        if (af == AddressFamily::IPv4)
        {
            RibEntry<uint32_t> ribRoute;
            ribRoute.prefix = readU32(prefix.addr);
            ribRoute.length = prefix.prefixLength;
            ribRoute.source = deriveOspfType(next.type);
            ribRoute.adminDistance = next.adminDistance;
            ribRoute.metric = next.cost;

            for (const auto& hop : merged)
                ribRoute.addNextHop(readU32(hop.nextHop.raw), hop.interfaceId);

            rib.addRoute(ribRoute);
        }
        else
        {
            RibEntry<__uint128_t> ribRoute;
            ribRoute.prefix = readU128(prefix.addr);
            ribRoute.length = prefix.prefixLength;
            ribRoute.source = deriveOspfType(next.type);
            ribRoute.adminDistance = next.adminDistance;
            ribRoute.metric = next.cost;

            for (const auto& hop : merged)
                ribRoute.addNextHop(readU128(hop.nextHop.raw), hop.interfaceId);

            rib.addRoute(ribRoute);
        }
    }

    st.selected = std::move(next);
    st.hasSelected = true;

    return ctx ? (routeChanged || ecmpChanged || (oldInterEffective != newInterEffective) || (oldInterEffective != newInterEffective))
               : (routeChanged || ecmpChanged);
}

std::vector<OspfRouteChange> OspfRib::recomputeLocked(const std::unordered_set<IPPrefix>& touched, uint32_t areaId, const std::unordered_set<IPPrefix>& ranges)
{
    AddressFamily af = process.getAF();
    uint32_t procId = process.getProcId();

    std::vector<OspfRouteChange> changes;
    changes.reserve(touched.size());
    
    RecomputeCtx ctx{
        .areaId = areaId,
        .ranges = ranges
    };

    for (const auto& prefix : touched)
    {
        ctx.intraChanged = false;
        ctx.interChanged = false;

        recomputeLocked(prefix, af, procId, &ctx);

        if (ctx.intraChanged && validateInterAreaSummaryEligibility(prefix))
            changes.push_back(std::move(ctx.interChange));
        if (ctx.interChanged && areaId == 0)
            changes.push_back(std::move(ctx.interChange));
    }

    return changes;
}

void OspfRib::recomputeLocked(const std::unordered_set<IPPrefix>& touched)
{
    AddressFamily af = process.getAF();
    uint32_t procId = process.getProcId();

    for (const auto& prefix : touched)
    {
        recomputeLocked(prefix, af, procId);
    }
}

bool OspfRib::globalRibContains(const IPPrefix& prefix) const
{
    if (prefix.af == AddressFamily::IPv4)
        return rib.lookup(readU32(prefix.addr));
    else
        return rib.lookup(readU128(prefix.addr));
}

bool OspfRib::validateInterAreaSummaryEligibility(const IPPrefix& prefix) const
{
    const bool useLocal = process.getConfigs().get<Config::Ospf::LRC_INTER_AREA_SUMMARY>().load();

    if (useLocal)
    {
        return  lookup(prefix);
    }

    return globalRibContains(prefix);
}
}
