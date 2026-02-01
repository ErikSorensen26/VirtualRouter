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
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto it = prefixStates.find(prefix);
    if (it == prefixStates.end() || !it->second.hasSelected)
        return nullptr;

    return &it->second.selected;
}

bool OspfRib::lpmLookup(const IPAddress& addr, uint32_t area) const
{
    std::shared_lock<std::shared_mutex> lock(mutex);

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

    std::unique_lock<std::shared_mutex> lock(mutex);

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

void OspfRib::replaceRoute(uint32_t areaId, const std::pair<IPPrefix, std::optional<OspfPath>>& path)
{
    auto areaList = areaIndex.find(areaId);
    bool areaFound = areaList != areaIndex.end();

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
            areaList->second.erase(path.first);
    }

    if (path.second.has_value())
    {
        prefixStates[path.first].canidates.push_back(path.second.value());
        areaIndex[areaId].insert(path.first);
        processWide.insert(path.first);
    }
    else if (areaFound && areaList->second.empty())
    {
        areaIndex.erase(areaId);
    }

    recomputeLocked(path.first, process.getAF(), process.getProcId());
}

void OspfRib::replaceExternals(const std::vector<std::pair<IPPrefix, OspfPath>>& paths)
{
    std::unordered_set<IPPrefix> touched;
    touched.reserve(paths.size());

    std::unique_lock lock(mutex);

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
    std::vector<std::pair<IPPrefix, OspfPath>> intra;

    std::shared_lock<std::shared_mutex> lock(mutex);
    for (const auto& [prefix, state] : prefixStates)
    {
        const OspfPath* bestPath = nullptr;
        for (const auto& canidate : state.canidates)
        {
            if (canidate.area == area && canidate.type == OspfRouteType::INTRA_AREA && canidate.cost < bestPath->cost)
                bestPath = &canidate;
        }
        if (bestPath)
            intra.push_back({prefix, *bestPath});
    }

    return intra;
}

void OspfRib::installDiscardRoute(const OspfDiscardKey& key, uint32_t cost, uint8_t ad)
{
    std::unique_lock<std::shared_mutex> lock(mutex);

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
    std::unique_lock<std::shared_mutex> lock(mutex);

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

    std::unique_lock<std::shared_mutex> lock(mutex);

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

bool OspfRib::recomputeLocked(const IPPrefix& prefix, AddressFamily af, uint32_t procId, IntraRecomputeCtx* intraCtx)
{
    auto it = prefixStates.find(prefix);
    if (it == prefixStates.end())
        return false;

    auto& st = it->second;

    auto isIntraEffective = [&](const OspfRoute& r) -> bool
    {
        if (r.type != OspfRouteType::INTRA_AREA)
            return false;
        if (intraCtx && r.area.has_value() && r.area.value() == intraCtx->areaId)
            return !r.suppressed;
        return true;
    };

    if (st.canidates.empty())
    {
        if (st.hasSelected)
        {
            const OspfRoute& old = st.selected;
            RouteSource src = deriveOspfType(old.type);

            if (af == AddressFamily::IPv4)
                rib.removeEntry(readU32(prefix.addr), prefix.prefixLength, src, procId);
            else
                rib.removeEntry(readU128(prefix.addr), prefix.prefixLength, src, procId);

            st.hasSelected = false;

            const bool oldIntraEffective = isIntraEffective(old);

            prefixStates.erase(it);

            if (oldIntraEffective)
            {
                if (intraCtx)
                {
                    intraCtx->change->prefix = old.prefix;
                    intraCtx->change->options = old.options;
                    intraCtx->change->cost = old.cost;
                    intraCtx->change->isRemoval = true;
                }
                return true;
            }
            return false;
        }

        prefixStates.erase(it);
        return false;
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
        if (p.type == best->type && p.cost == best->cost)
            next.paths.push_back(p);
    }
#ifndef NDEBUG
    for (const auto& p : next.paths)
         assert(p.suppressed == next.suppressed);
#endif

    const bool hadOld = st.hasSelected;

    bool routeChanged = true;
    bool ecmpChanged = true;

    if (hadOld)
    {
        const OspfRoute& old = st.selected;

        routeChanged =
            old.type != next.type ||
            old.cost != next.cost ||
            old.options != next.options ||
            old.adminDistance != next.adminDistance ||
            old.area != next.area ||
            old.suppressed != next.suppressed;

        ecmpChanged = (old.paths != next.paths);
    }

    const bool oldIntraEffective = hadOld ? isIntraEffective(st.selected) : false;
    const bool newIntraEffective = isIntraEffective(next);

    if (oldIntraEffective != newIntraEffective)
    {
        st.selected = std::move(next);
        st.hasSelected = true;

        if (intraCtx)
        {
            intraCtx->change->prefix = prefix;
            intraCtx->change->options = st.selected.options;
            intraCtx->change->cost = st.selected.cost;
            intraCtx->change->isRemoval = oldIntraEffective;
        }
        return true;
    }

    if (newIntraEffective && (!hadOld || routeChanged))
    {
        st.selected = std::move(next);
        st.hasSelected = true;

        if (intraCtx)
        {
            intraCtx->change->prefix = prefix;
            intraCtx->change->options = st.selected.options;
            intraCtx->change->cost = st.selected.cost;
            intraCtx->change->isRemoval = false;
        }
        return true;
    }

    if (!hadOld || routeChanged || ecmpChanged)
    {
        const auto& merged = mergeEcmpNextHops(next.paths);

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

    return false;
}

std::vector<OspfRouteChange> OspfRib::recomputeLocked(const std::unordered_set<IPPrefix>& touched, uint32_t areaId, const std::unordered_set<IPPrefix>& ranges)
{
    AddressFamily af = process.getAF();
    uint32_t procId = process.getProcId();

    std::vector<OspfRouteChange> changes;
    changes.reserve(touched.size());

    IntraRecomputeCtx ctx = {
        .areaId = areaId,
        .ranges = ranges,
        .change = &changes.emplace_back()
    };

    for (const auto& prefix : touched)
    {
        if (recomputeLocked(prefix, af, procId, &ctx))
            ctx.change = &changes.emplace_back();
    }

    changes.pop_back();
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
}
