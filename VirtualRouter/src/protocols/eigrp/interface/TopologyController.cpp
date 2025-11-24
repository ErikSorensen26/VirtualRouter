// EigrpInterfaceTopology.cpp

#include "TopologyController.h"
#include <DuelEngine.h>
#include <TopologyTable.h>
#include "NeighborTable.h"
#include "EigrpInterface.h"
#include <Eigrp.h>
#include <Interface.h>

namespace Eigrp
{
TopologyController::TopologyController(NeighborTable& ntable, DuelEngine& duel, EigrpInterface& iface) : ntable(ntable), duel(duel), iface(iface) {}

std::vector<const RouteInfo*> TopologyController::getAllRoutes()
{
    return duel.topologyTable.getAllRoutes();
}

std::unordered_map<IPPrefix, TopologyEntry*>& TopologyController::getTopologies()
{
    return duel.topologyTable.entries();
}

std::vector<const RouteInfo*> TopologyController::filterAdvertisableRoutes(const std::vector<const RouteInfo*> routes)
{
    std::vector<const RouteInfo*> filtered;
    if (routes.empty() || iface.configs->isPassive.load(std::memory_order_relaxed)) return filtered;

    auto& cfgMgr = iface.getBase().getGlobalConfigMgr();
    const auto& stubCfg = cfgMgr.getStubConfig();
    const bool splitHorizon = iface.configs->splitHorizon.load(std::memory_order_relaxed);

    for (const auto* route : routes)
    {
        if (!route)
            continue;

        if (route->topology && (route->topology->summaries.count(iface.interfaceKey) > 0 || route->topology->state == TopologyEntry::State::ACTIVE))
            continue;

        if (splitHorizon && route->routeInfo.originInterface == iface.interfaceKey && route->routeInfo.routeType != RouteType::SUMMARY)
            continue;

        if (stubCfg.isStub)
        {
            bool allow = true;
            switch (route->routeInfo.routeType)
            {
                case RouteType::CONNECTED:
                    allow = stubCfg.advertiseConnected;
                    break;
                case RouteType::STATIC:
                    allow = stubCfg.advertiseStatic;
                    break;
                case RouteType::EXTERNAL:
                    allow = stubCfg.advertiseRedistributed;
                    break;
                case RouteType::SUMMARY:
                    allow = stubCfg.advertiseSummary;
                    break;
                default:
                    break;
            }
            if (!allow)
                continue;
        }

        filtered.emplace_back(route);
    }
    return filtered;
}

void TopologyController::onNeighborDown(const IPAddress& neighborIp)
{
    // Collect affected routes
    std::vector<std::pair<TopologyEntry*, RouteInfo*>> affectedRoutes;
    for (auto& [destination, entry] : duel.topologyTable.entries())
    {
        // If this neighbor was advertising the route
        if (auto it = entry->routesByNeighbor.find(neighborIp); it != entry->routesByNeighbor.end())
        {
            affectedRoutes.push_back({entry, &it->second});
        }
    }

    // Trigger Active for routes with no feasible successor
    std::vector<TopologyEntry*> affectedTopologies;
    for (const auto& [top, route] : affectedRoutes)
    {
        if (top && route)
        {
            duel.topologyTable.markRouteUnreachable(*route, neighborIp, *top);
            affectedTopologies.push_back(top);
        }
    }

    // Remove the neighbor routes from topology
    duel.topologyTable.pruneNeighbor(neighborIp);

    duel.updateSuccessors(affectedTopologies, neighborIp);
}

void TopologyController::processReceivedRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor)
{
    duel.processReceivedRoutes(routes, neighbor);
}

void TopologyController::processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor)
{
    duel.processReceivedActiveRoutes(routes, neighbor);
}

void TopologyController::processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor, uint32_t recvSeq)
{
    duel.processReceivedQueryRoutes(routes, neighbor, recvSeq);
}

void TopologyController::processSIAReply(Neighbor& neighbor, uint32_t seq)
{
    duel.processSIAReply(neighbor, seq);
}

TopologyEntry* TopologyController::findEntry(const IPPrefix& prefix)
{
    return duel.topologyTable.find(prefix);
}
}
