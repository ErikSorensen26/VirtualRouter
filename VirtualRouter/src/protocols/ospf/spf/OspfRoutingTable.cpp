// OspfRoutingTable.cpp

#include "OspfRoutingTable.h"
#include <OspfProcess.h>
#include <OspfArea.h>
#include <VirtualRouter.h>
#include <RoutingTable.hpp>

#include <mutex>

namespace OSPF
{
OspfRib::OspfRib(OspfProcess& p)
    : process(p), rib(p.routingInstance->routingTable) {}

const OspfRoute* OspfRib::lookup(const IPPrefix& prefix) const
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto it = globalRoutes.find(prefix);
    return it != globalRoutes.end() ? &it->second : nullptr;
}

std::vector<OspfRouteChange> OspfRib::replaceArea(uint32_t areaId, const std::vector<std::pair<IPPrefix, OspfPath>>& paths)
{
    std::unordered_set<IPPrefix> touched;

    std::unique_lock lock(mutex);

    auto idx = areaIndex.find(areaId);
    if (idx != areaIndex.end())
    {
        for (const auto& prefix : idx->second)
        {
            auto& vec = allPaths[prefix];
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [areaId](const OspfPath& p) { return p.area == areaId && p.type == OspfRouteType::INTRA_AREA; }),
                vec.end());
            touched.insert(prefix);
        }
        areaIndex.erase(idx);
    }

    for (const auto& [prefix, path] : paths)
    {
        allPaths[prefix].push_back(path);
        areaIndex[areaId].insert(prefix);
        touched.insert(prefix);
    }

    return recomputeLocked(touched);
}

std::vector<OspfRouteChange> OspfRib::recomputeLocked(const std::unordered_set<IPPrefix>& touched)
{
    AddressFamily af = process.getAF();
    uint32_t procId = process.getProcId();

    std::vector<OspfRouteChange> changes;

    for (const auto& prefix : touched)
    {
        auto it = allPaths.find(prefix);
        if (it == allPaths.end() || it->second.empty())
        {
            if (auto git = globalRoutes.find(prefix); git != globalRoutes.end())
            {
                changes.push_back({
                    .prefix = prefix,
                    .options = globalRoutes[prefix].options,
                    .cost = globalRoutes[prefix].cost,
                    .isRemoval = true
                });
                globalRoutes.erase(prefix);
            }

            af == AddressFamily::IPv4
                ? rib.removeEntry(readU32(prefix.addr), prefix.prefixLength, RouteSource::OSPF, procId)
                : rib.removeEntry(readU128(prefix.addr), prefix.prefixLength, RouteSource::OSPF, procId);

            continue;
        }

        const auto& paths = it->second;
        const OspfPath* best = nullptr;

        for (const auto& p : paths)
        {
            if (!best || p.type < best->type || (p.type == best->type && p.cost < best->cost))
            {
                best = &p;
            }
        }

        OspfRoute route;
        route.prefix = prefix;
        route.options = best->options;
        route.type = best->type;
        route.cost = best->cost;

        for (const auto& p : paths)
        {
            if (p.type == route.type && p.cost == route.cost)
                route.paths.push_back(p);
        }

        auto git = globalRoutes.find(prefix);

        bool change = (git == globalRoutes.end() || git->second.cost != route.cost);
        bool ribChange = (change || git->second.paths != route.paths);

        if (change)
        {
            changes.push_back({
                .prefix = route.prefix,
                .options = route.options,
                .cost = route.cost,
                .isRemoval = false
            });
        }

        if (ribChange)
        {
            if (af == AddressFamily::IPv4)
            {
                RibEntry<uint32_t> ribRoute;
                ribRoute.prefix = readU32(prefix.addr);
                ribRoute.length = prefix.prefixLength;
                ribRoute.source = RouteSource::OSPF;
                ribRoute.adminDistance = route.adminDistance;
                ribRoute.metric = route.cost;

                for (const auto& hop : best->nextHops)
                    ribRoute.addNextHop(readU32(hop.nextHop.raw), hop.interfaceId);

                rib.addRoute(ribRoute);
            }
            else
            {
                RibEntry<__uint128_t> ribRoute;
                ribRoute.prefix = readU128(prefix.addr);
                ribRoute.length = prefix.prefixLength;
                ribRoute.source = RouteSource::OSPF;
                ribRoute.adminDistance = route.adminDistance;
                ribRoute.metric = route.cost;

                for (const auto& hop : best->nextHops)
                    ribRoute.addNextHop(readU128(hop.nextHop.raw), hop.interfaceId);

                rib.addRoute(ribRoute);
            }
        }

        globalRoutes[prefix] = std::move(route);
    }

    return changes;
}
}
