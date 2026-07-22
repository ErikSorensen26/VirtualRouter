// EigrpInterfaceTopology.cpp

#include "TopologyController.h"
#include "eigrp/topology/DuelEngine.h"
#include "eigrp/topology/TopologyTable.h"
#include "eigrp/rtp/NeighborTable.h"
#include "EigrpInterface.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/EigrpTypes.hpp"

namespace routing::eigrp
{
TopologyController::TopologyController(NeighborTable& ntable, DuelEngine& duel, EigrpInterface& iface) : ntable(ntable), duel(duel), iface(iface) {}

uint64_t TopologyController::getLocalMetric()
{
    return iface.localMetric.load(std::memory_order_relaxed);
}

std::unordered_map<types::IPPrefix, TopologyEntry>& TopologyController::getTopologies()
{
    return duel.topologyTable.entries();
}

std::vector<const RouteInfo*> TopologyController::getAdvertisableRoutes()
{
    std::vector<const RouteInfo*> routes;
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load())
        return routes;

    auto stubCfg = getStubConfig(iface.getConfigs());
    const bool splitHorizon = iface.configs.get<config::EigrpInterface::SPLIT_HORIZON>().load();

    for (const auto& [_, entry] : duel.topologyTable.entries())
    {
        if (entry.isSuppressed(iface.interfaceKey) || entry.state == TopologyEntry::State::ACTIVE)
            continue;

        auto it = entry.routesBySource.find(entry.bestNeighbor);
        if (it == entry.routesBySource.end())
            continue;

        const RouteInfo* route = &it->second;

        if (splitHorizon && route->routeInfo.originInterface == iface.interfaceKey
            && route->routeInfo.routeType != RouteType::SUMMARY)
            continue;

        if (stubCfg.isStub)
        {
            bool allow = true;
            switch (route->routeInfo.routeType)
            {
                case RouteType::CONNECTED: allow = stubCfg.advertiseConnected; break;
                case RouteType::STATIC:    allow = stubCfg.advertiseStatic; break;
                case RouteType::EXTERNAL:  allow = stubCfg.advertiseRedistributed; break;
                case RouteType::SUMMARY:   allow = stubCfg.advertiseSummary; break;
                default: break;
            }
            if (!allow) continue;
        }

        routes.push_back(route);
    }
    return routes;
}

std::vector<const RouteInfo*> TopologyController::filterAdvertisableRoutes(const std::vector<const RouteInfo*>& routes)
{
    std::vector<const RouteInfo*> filtered;
    if (routes.empty() || iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return filtered;

    auto stubCfg = getStubConfig(iface.getConfigs());
    const bool splitHorizon = iface.configs.get<config::EigrpInterface::SPLIT_HORIZON>().load();

    for (const auto* route : routes)
    {
        if (!route)
            continue;

        if (route->topology && (route->topology->isSuppressed(iface.interfaceKey) || route->topology->state == TopologyEntry::State::ACTIVE))
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

void TopologyController::onNeighborDown(Neighbor& neighbor)
{
    const types::IPAddress& neighborIp = neighbor.ipAddress;

    std::vector<TopologyEntry*> affectedTopologies;
    for (auto& [_, entry] : duel.topologyTable.entries())
    {
        if (auto it = entry.routesBySource.find(neighborIp); it != entry.routesBySource.end())
        {
            duel.topologyTable.markRouteUnreachable(it->second, neighborIp, entry);
            affectedTopologies.push_back(&entry);
        }
    }

    neighbor.recvInitSeq.store(0, std::memory_order_release);
    neighbor.srtt.store(1.0, std::memory_order_release);
    neighbor.rttvar.store(0.5, std::memory_order_release);
    neighbor.rto.store(1.5, std::memory_order_release);
    neighbor.clearReliable();

    // Remove the neighbor routes from topology
    duel.removeActiveNeighbor(neighborIp);
    duel.updateSuccessors(affectedTopologies);
    duel.topologyTable.pruneNeighbor(neighborIp);
}

void TopologyController::refreshSuppression(std::vector<TopologyEntry*>& entries)
{
    duel.refreshSuppression(entries, &iface);
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

void TopologyController::markRouteUnreachable(RouteInfo& route, const types::IPAddress& neighborIp, TopologyEntry& entry)
{
    duel.topologyTable.markRouteUnreachable(route, neighborIp, entry);
}

TopologyEntry* TopologyController::findEntry(const types::IPPrefix& prefix)
{
    return duel.topologyTable.find(prefix);
}

TopologyEntry& TopologyController::ensure(const types::IPPrefix& prefix)
{
    return duel.topologyTable.ensure(prefix);
}
} // namespace routing
