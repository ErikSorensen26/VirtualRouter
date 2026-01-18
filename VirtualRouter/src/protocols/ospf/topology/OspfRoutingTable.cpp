// OspfRoutingTable.cpp

#include "OspfRoutingTable.h"

#include <OspfProcess.h>
#include <OspfTopology.h>
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

OspfRib::OspfRib(Topology& p)
    : topology(p), rib(topology.process.routingInstance->routingTable) {}

const OspfRoute* OspfRib::lookup(const IPPrefix& prefix) const
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto it = prefixStates.find(prefix);
    if (it == prefixStates.end() || !it->second.hasSelected)
        return nullptr;

    return &it->second.selected;
}

std::vector<OspfRouteChange> OspfRib::replaceArea(uint32_t areaId, const std::vector<std::pair<IPPrefix, OspfPath>>& paths)
{
    std::unordered_set<IPPrefix> touched;
    touched.reserve(paths.size());

    std::unique_lock lock(mutex);

    auto idx = areaIndex.find(areaId);
    if (idx != areaIndex.end())
    {
        for (const auto& prefix : idx->second)
        {
            auto psIt = prefixStates.find(prefix);
            if (psIt == prefixStates.end())
                continue;

            auto& cand = psIt->second.canidates;

            cand.erase(
                std::remove_if(cand.begin(), cand.end(),
                    [areaId](const OspfPath& p)
                    {
                        return p.area.has_value() && p.area.value() == areaId;
                    }),
                cand.end());

            touched.insert(prefix);
        }
        areaIndex.erase(idx);
    }

    for (const auto& [prefix, path] : paths)
    {
        prefixStates[prefix].canidates.push_back(path);
        areaIndex[areaId].insert(prefix);
        touched.insert(prefix);
    }

    return recomputeLocked(touched);
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
                    return !p.area.has_value();
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

    recomputeLocked({path.first});
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
                    return !p.area.has_value();
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
                    return !p.area.has_value();
                }),
            cand.end());
        processWide.erase(path.first);
    }

    if (path.second.has_value())
    {
        prefixStates[path.first].canidates.push_back(path.second.value());
        processWide.insert(path.first);
    }

    recomputeLocked({path.first});
}

std::vector<OspfRouteChange> OspfRib::recomputeLocked(const std::unordered_set<IPPrefix>& touched)
{
    AddressFamily af = topology.process.getAF();
    uint32_t procId = topology.process.getProcId();

    std::vector<OspfRouteChange> changes;
    changes.reserve(touched.size());

    for (const auto& prefix : touched)
    {
        auto& st = prefixStates.at(prefix);

        if (st.canidates.empty())
        {
            if (st.hasSelected)
            {
                const OspfRoute& old = st.selected;

                if (old.type == OspfRouteType::INTRA_AREA)
                {
                    changes.push_back({
                        .prefix = old.prefix,
                        .options = old.options,
                        .cost = old.cost,
                        .isRemoval = true
                    });
                }

                st.hasSelected = false;
                RouteSource src = deriveOspfType(st.selected.type);

                af == AddressFamily::IPv4
                    ? rib.removeEntry(readU32(prefix.addr), prefix.prefixLength, src, topology.tid, procId)
                    : rib.removeEntry(readU128(prefix.addr), prefix.prefixLength, src, topology.tid, procId);
            }

            prefixStates.erase(prefix);

            continue;
        }

        const OspfPath* best = nullptr;
        for (const auto& p : st.canidates)
        {
            if (!best || isBetterPath(p, *best))
                best = &p;
        }
        if (!best)
            continue;

        OspfRoute next{};
        next.prefix = prefix;
        next.type = best->type;
        next.cost = best->cost;
        next.area = best->area;
        next.adminDistance = best->adminDistance;
        next.options = best->options;

        next.paths.reserve(st.canidates.size());
        for (const auto& p : st.canidates)
        {
            if (p.type == best->type && p.cost == best->cost)
                next.paths.push_back(p);
        }

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
                (old.area.has_value() && next.area.has_value() && old.area.value() != next.area.value());

            ecmpChanged = (old.paths != next.paths);
        }

        const bool oldIntra = hadOld && (st.selected.type == OspfRouteType::INTRA_AREA);
        const bool newIntra = (next.type == OspfRouteType::INTRA_AREA);

        if (oldIntra != newIntra)
        {
            changes.push_back({
                .prefix = next.prefix,
                .options = next.options,
                .cost = next.cost,
                .isRemoval = oldIntra
            });
        }
        else if (newIntra && (!hadOld || routeChanged))
        {
            changes.push_back({
                .prefix = next.prefix,
                .options = next.options,
                .cost = next.cost,
                .isRemoval = false
            });
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
    }

    return changes;
}
}
