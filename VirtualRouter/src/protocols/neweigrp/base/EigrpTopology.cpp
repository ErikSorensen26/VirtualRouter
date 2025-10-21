// EigrpTopology.cpp

#include "EigrpTopology.h"
#include "Eigrp.h"
#include <RoutingTable.h>
#include "EigrpConfigManager.h"
#include <VirtualRouter.h>

namespace Protocol
{
EigrpTopology::EigrpTopology(Eigrp& base) : base(base), routingTable(base.routingInstance->routingTable) {}

void EigrpTopology::recalculateRoutes()
{
    uint8_t variance = base.configMgr.getVariance();
    if (variance == 0) return; // Invalid variance

    // Iterate through the topology table and recalculate all routes.
    for (auto& [destination, entry] : topologyTable->getTopologyEntries())
    {
        // Find all feasible successors within the Variance
        for (const auto& [neighbor, routeInfo] : entry->routesByNeighbor)
        {
            if (routeInfo.feasibleDistance <= entry->bestFD && routeInfo.feasibleDistance <= entry->bestFD * variance && routeInfo.reportedDistance < entry->bestFD)
            {
                // Add or update route in the routing table
                RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp(routeInfo.eigrpInterface->interfaceKey);
                newRoute->network = {destination.addr, base.addressFamily};
                newRoute->mask = entry->prefixLength;
                newRoute->nextHop = neighbor;
                newRoute->metric = routeInfo.feasibleDistance;
                newRoute->routeType = RoutingTable::Eigrp::RouteType::INTERNAL;

                base.routingInstance->routingTable.addEigrp(newRoute, base.addressFamily, base.asNumber);
            }
        }
    }
}

void EigrpTopology::recalculateRouteMetrics()
{
    auto eigrpRoutes = routingTable.getAllEigrpRoutes(base.addressFamily, base.asNumber);
    for (auto* route : eigrpRoutes)
    {
        
    }
}

void EigrpTopology::addRouteMetric(uint32_t localCost, RoutingTable::Eigrp* route)
{
    // Calculate Feasible Distance and Composite Metric
    uint32_t neighborRD = static_cast<uint32_t>(base.metrics.calculateMetric(route->bandwidth, route->load, route->delay, route->reliability));
    route->reportedDistance = neighborRD;

    // Calculate FD = Local Link Cost + RD
    route->feasibleDistance = localCost + route->reportedDistance;

    // Calculate the composite metric for internal use
    route->metric = localCost;

    // Set the administrative distance
    route->adminDistance = (route->routeType == RoutingTable::Eigrp::RouteType::INTERNAL || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
        ? base.configMgr.getAD()
        : base.configMgr.getExternalAD();
}

void EigrpTopology::updateRoutingTableForConnected(EigrpInterface* eigrpInterface)
{
    std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes{};
    {
        if (!base.routingInstance) return;

        std::unordered_set<IPPrefix> connectedNetworks;
        
        std::unordered_map<uint32_t, EigrpInterface*> currentEigrpInterface;
        // get Eigrp Interface list
        if (eigrpInterface)
        {
            if (!eigrpInterface->currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            {
                auto& ipInfo = eigrpInterface->currentInterface->configs;
                currentEigrpInterface[ipInfo.id] = eigrpInterface;
            }
        }

        {
            auto updateRoutesForInterface([&](EigrpInterface* eigrpInterfacePtr)
            {
                if (!eigrpInterfacePtr || !eigrpInterfacePtr->currentInterface || !base.routingInstance) return;

                if (!eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed))
                {
                    auto& interfaceInfo = eigrpInterfacePtr->currentInterface->configs;
                    uint32_t eigrpBw = base.configMgr.getLowestBandwidth();
                    uint16_t mtu = base.addressFamily == AddressFamily::IPv4 
                        ? interfaceInfo.ipv4.mtu.load(std::memory_order_relaxed)
                        : interfaceInfo.ipv6.mtu.load(std::memory_order_relaxed);
                    uint8_t connectedMask;
                    uint8_t connectedNetwork[16];

                    {
                        if (base.addressFamily == AddressFamily::IPv4)
                        {
                            connectedMask = interfaceInfo.ipv4.getMask();
                            uint8_t address[4];
                            interfaceInfo.ipv4.getAddress(address);
                            Functions::computeNetworkAddress(connectedNetwork, address, connectedMask, AddressFamily::IPv4);
                        }
                        else if (base.addressFamily == AddressFamily::IPv6)
                        {
                            IPAddress address;
                            connectedMask = interfaceInfo.ipv6.getGlobalUnicastPair(address.raw);
                            if (address.v6 == 0) return; // No global address available on this interface
                            Functions::computeNetworkAddress(connectedNetwork, address.raw, connectedMask, AddressFamily::IPv6);
                        }
                        else return;
                    }

                    // Compute the connected network
                    connectedNetworks.emplace(connectedNetwork, connectedMask, base.addressFamily);

                    // Create EIGRP route entry
                    RoutingTable::Eigrp* connectedRoute = new RoutingTable::Eigrp(eigrpInterfacePtr->interfaceKey);
                    connectedRoute->bandwidth = ( 10000000 / eigrpBw ) * 256;
                    connectedRoute->delay = 0;
                    connectedRoute->hopCount = 0;
                    connectedRoute->mtu = mtu;
                    connectedRoute->reliability = 255;
                    connectedRoute->load = base.configMgr.getVariance();
                    connectedRoute->mask = connectedMask;
                    connectedRoute->routeType = RoutingTable::Eigrp::RouteType::CONNECTED;

                    std::memcpy(connectedRoute->network.raw, connectedNetwork, static_cast<uint8_t>(base.addressFamily));
                    if (eigrpInterface && eigrpInterface->configs->nextHopSelf.load(std::memory_order_relaxed))
                        { connectedRoute->nextHop = eigrpInterface->getInterfaceIp(); }
                    else
                        { std::memset(connectedRoute->nextHop.raw, 0, 16); } // indicates directly connected

                    auto existingRoute = routingInstance->routingTable.getEigrpRoute(connectedNetwork, connectedRoute->mask, addressFamily, asNumber);

                    bool hasChanged = !existingRoute ||
                                    existingRoute->feasibleDistance != connectedRoute->feasibleDistance ||
                                    existingRoute->mask != connectedRoute->mask;

                    if (hasChanged)
                    {
                        // Insert into Routing Table
                        routingTable.addEigrp(connectedRoute, addressFamily, asNumber);
                        
                        // Advertise the connected route to eigrp neighbors
                        updatedRoutes.emplace_back(connectedRoute, false);
                    }
                    else
                    {
                        delete connectedRoute;
                        connectedRoute = nullptr;
                    }
                }
                else
                {
                    IPAddress address;
                    uint8_t mask;
                    if (addressFamily == AddressFamily::IPv4)
                    {
                        eigrpInterfacePtr->currentInterface->configs.ipv4.getAddress(address.raw);
                        mask = eigrpInterfacePtr->currentInterface->configs.ipv4.getMask();
                    }
                    else
                    {
                        mask = eigrpInterfacePtr->currentInterface->configs.ipv6.getGlobalUnicastPair(address.raw);
                        if (address.v6 == 0) return;
                    }
                    auto removalRoute = routingInstance->routingTable.getEigrpRoute(address.raw, mask, addressFamily, asNumber);
                    if (removalRoute)
                    {
                        routingInstance->routingTable.removeEigrp(address.raw, mask, addressFamily, asNumber);
                        updatedRoutes.emplace_back(removalRoute, true);
                    }
                }
            });

            {
                if (eigrpInterface)
                {
                    updateRoutesForInterface(eigrpInterface);
                }
                else
                {
                    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                    for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
                    {
                        updateRoutesForInterface(eigrpInterfacePtr);
                    }
                }
            }
        }

        // Remove any routes that are no longer exist
        for (const auto& route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
        {
            if (route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED)
            {
                if (!connectedNetworks.contains(IPPrefix{route->network.raw, route->mask, addressFamily}))
                {
                    updatedRoutes.emplace_back(route, true);
                }
            }
        }
    }

    // Notify neighbors
    notifyRoutingChange(updatedRoutes);

    // Remove routes from table after neighbor.
    for (const auto& route : updatedRoutes)
    {
        if (route.withdraw)
        {
            routingInstance->routingTable.removeEigrp(route.route->network.raw, route.route->mask, addressFamily, asNumber);
        }
    }
}

void Eigrp::notifyRoutingChange(const std::vector<EigrpConfigs::RoutingUpdate>& changedRoutes)
{
    Logger::getInstance().info() << "Notifying all neighbors for route changes" << std::endl;

    {
        std::shared_lock<std::shared_mutex> lock(interfaceMutex);

        // Adjust summaries based on added/removed routes
        for (const auto [_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            if (!eigrpInterfacePtr || !eigrpInterfacePtr->currentInterface) continue;
            if (eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed)) continue;
            std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
            bool hasMulticast = false;
            if (!changedRoutes.empty())
            {
                {
                    // Check all neighbors for unicast and multicast
                    std::shared_lock<std::shared_mutex> neighborLock(eigrpInterfacePtr->neighborMutex);
                    for (const auto& [address, neighbor] : eigrpInterfacePtr->neighbors)
                    {
                        if (neighbor->unicast)
                        {
                            unicastNeighbors.push_back(neighbor);
                        }
                        else
                        {
                            hasMulticast = true;
                        }
                    }
                }

                for (const auto& neighbor : unicastNeighbors)
                {
                    eigrpInterfacePtr->sendUpdateToNeighbor(neighbor, changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                }
                if (hasMulticast && eigrpInterfacePtr->configs->multicastEnabled.load(std::memory_order_relaxed))
                {
                    eigrpInterfacePtr->sendUpdateToNeighbor(nullptr, changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                }
            }
        }
    }
}
}
