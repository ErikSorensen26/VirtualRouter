// RouteManager.cpp

#include <RouteManager.h>
#include "EigrpCore.h"

namespace Eigrp
{
void RouteManager::installConnected(const EigrpInterface* eigrpInterface)
{
    std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes{};
    {
        if (!base.routingInstance) return;

        std::unordered_set<IPPrefix> connectedNetworks;
        
        std::unordered_map<uint32_t, EigrpInterface*> currentEigrpInterface;
        // get Eigrp Interface list
        if (eigrpInterface)
        {
            if (!eigrpInterface->getIface()->shutdownFlag.load(std::memory_order_relaxed))
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

void EigrpConfig::redistributeRoute(const uint8_t* destination, uint8_t mask, const uint16_t protocol)
{
    RoutingTable& routingTable = base.routingInstance->routingTable;
    auto route = routingTable.getEigrpRoute(destination, mask, base.getAF(), base.getAS());

    if (route)
    {
        // Convert route to external EIGRP and notify neighbors
        RoutingTable::Eigrp* externalRoute = route;
        externalRoute->routeType = RoutingTable::Eigrp::RouteType::EXTERNAL;
        externalRoute->metric += configs.redistributionMetricOffset.load(std::memory_order_relaxed);

        routingTable.addEigrp(externalRoute, base.getAF(), base.getAS());
        notifyRoutingChange({{externalRoute, true}});
    }
}

void EigrpInterface::synchronizeRoute(const TopologyTable::TopologyEntry& entry)
{
    
    // Find the successor route
    auto successorIt = std::find_if(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
                                    [](const auto &pair) { return pair.second.isSuccessor; });

    if (successorIt != entry->routesByNeighbor.end())
    {
        // Update the routing table accordingly
        RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp(interfaceKey);
        newRoute->bandwidth = successorIt->second.bandwidthMetric;
        newRoute->delay = successorIt->second.delayMetric;
        newRoute->hopCount = successorIt->second.hopCount;
        newRoute->mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
        newRoute->reliability = 255;
        newRoute->load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
        std::memcpy(newRoute->network.raw, prefix.addr, static_cast<uint8_t>(eigrpProcess.addressFamily));
        newRoute->network.isV6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
        newRoute->mask = entry->prefixLength;
        newRoute->nextHop = successorIt->second.nextHop;
        newRoute->metric = successorIt->second.feasibleDistance;
        newRoute->routeType = successorIt->second.routeType;

        // Update the global routing table
        if (eigrpProcess.routingInstance->routingTable.addEigrp(newRoute, eigrpProcess.addressFamily, eigrpProcess.asNumber))
        {
            eigrpProcess.notifyRoutingChange({{newRoute, false}});
        }
    }
    else
    {
        eigrpProcess.routingInstance->routingTable.removeEigrp(prefix.addr, prefix.prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
    }
}
}
