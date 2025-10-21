// EigrpInterfaceSummary.cpp

#include "EigrpInterfaceSummary.h"

namespace Protocol
{
void EigrpInterface::announceSummary(RoutingTable::Eigrp* summaryRoute)
{
    // Construct the route to advertise
    if (!summaryRoute) return;
    
    std::vector<EigrpConfigs::NeighborInfo*> neighborsToNotify;
    bool hasMulticast = false;

    // Collect neigbors
    {
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto& [address, neighbor] : neighbors)
        {
            if (neighbor->unicast)
            {
                neighborsToNotify.push_back(neighbor);
            }
            else
            {
                hasMulticast = true;
            }
        }
    }

    // Send unicast updates
    for (const auto& neighbor : neighborsToNotify)
    {
        sendUpdateToNeighbor(neighbor, {{summaryRoute, false}}, EigrpConfigs::UpdateType::PARTIAL);
    }

    // Send multicast update if enabled
    if (hasMulticast && configs->multicastEnabled.load(std::memory_order_relaxed))
    {
        sendUpdateToNeighbor(nullptr, {{summaryRoute, false}}, EigrpConfigs::UpdateType::PARTIAL);
    }
}

void EigrpInterface::withdrawSummary(RoutingTable::Eigrp* route)
{
    if (!route) return;

    std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
    bool hasMulticast = false;

    // Iterate through neighbors
    {
        IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);

        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto& [_, neighbor] : neighbors) 
        {
            // Mark for removal in neighbor state
            {
                std::unique_lock<std::shared_mutex> neighborInfoLock(neighbor->neighborDataMutex);
                auto it = neighbor->advertisedRoutes.find(key);
                if (it != neighbor->advertisedRoutes.end())
                {
                    it->second.removePending = true;
                }
            }

            if (neighbor->unicast)
                unicastNeighbors.push_back(neighbor);
            else
                hasMulticast = true;
        }
    }

    // Build withdrawal route (infinite metric)
    RoutingTable::Eigrp withdrawl(interfaceKey);
    withdrawl.network = route->network;
    withdrawl.mask = route->mask;
    withdrawl.routeType = RoutingTable::Eigrp::RouteType::SUMMARY;
    withdrawl.metric = std::numeric_limits<uint32_t>::max();

    // Send withdraw update to the neighbor
    for (const auto& neighbor : unicastNeighbors)
    {
        sendUpdateToNeighbor(neighbor, {{&withdrawl, true}}, EigrpConfigs::UpdateType::PARTIAL);
    }
    if (hasMulticast && configs->multicastEnabled.load(std::memory_order_relaxed))
    {
        sendUpdateToNeighbor(nullptr, {{&withdrawl, true}}, EigrpConfigs::UpdateType::PARTIAL);
    }
}

void EigrpInterface::clearAutoSummaries()
{
    std::unique_lock<std::shared_mutex> lock(configs->configsMutex);

    for (auto it = configs->summaryRoutes.begin(); it != configs->summaryRoutes.end();)
    {
        if (it->isAuto)
        {
            // Withdraw form neighbors
            withdrawSummaryRoute(it->summary);
            
            // Remove from global routing table
            eigrpProcess.routingInstance->routingTable.removeEigrp(
                it->summary->network.raw,
                it->summary->mask,
                eigrpProcess.addressFamily,
                eigrpProcess.asNumber
            );

            //TODO remove null0 discard route

            // Free route
            delete it->summary;

            // Erase entry
            it = configs->summaryRoutes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void EigrpInterface::addManualSummary(const uint8_t* network, uint8_t mask, bool isAuto)
{
    if (Functions::compareNetworkWithMask(network, mask, eigrpProcess.addressFamily))
        return; // Mask invalid

    // Don't add if already summarized
    if (isRouteSummarized(network, mask)) 
        return;

    uint32_t minBandwidth = std::numeric_limits<uint32_t>::max();
    uint32_t minDelay = std::numeric_limits<uint32_t>::max();

    const auto& allRoutes = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(
        eigrpProcess.addressFamily, eigrpProcess.asNumber
    );

    for (const auto* route : allRoutes)
    {
        if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
            continue;
        if (isRouteSummarized(route->network.raw, route->mask))
        {
            if (route->bandwidth < minBandwidth)
                minBandwidth = route->bandwidth;

            if (route->delay < minDelay)
                minDelay = route->delay;
        }
    }

    // Create summary route
    RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
    std::memcpy(route->network.raw, network, static_cast<uint8_t>(eigrpProcess.addressFamily));
    route->network.isV6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
    route->mask = mask;
    route->routeType = RoutingTable::Eigrp::RouteType::SUMMARY;
    route->hopCount = 0;
    route->delay = 0;
    route->bandwidth = ( 10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed)) * 256;
    route->mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
        ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
        : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
    route->reliability = 255;
    route->load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
    route->adminDistance = eigrpProcess.configs.adminDistance.load(std::memory_order_relaxed);
    if (configs->nextHopSelf.load(std::memory_order_relaxed))
        route->nextHop = getInterfaceIp();

    eigrpProcess.routingInstance->routingTable.addEigrp(route, eigrpProcess.addressFamily, eigrpProcess.asNumber);

    // Add new summary route
    {
        EigrpConfigs::SummaryRoute entry;
        std::unique_lock<std::shared_mutex> configsLock(configs->configsMutex);
        entry.summary = route;
        entry.isAuto = isAuto;
        configs->summaryRoutes.push_back(std::move(entry));
    }

    // Update interface to advertise the new summary route
    advertiseSummaryRoute(route);
}

void EigrpInterface::removeManuelSummary(const IPAddress& network, uint8_t mask)
{
    std::unique_lock<std::shared_mutex> lock(configs->configsMutex);

    auto list = configs->summaryRoutes;
    for (auto it = list.begin(); it != list.end();)
    {
        RoutingTable::Eigrp* route = it->summary;
        if (route->network == network && route->mask == mask)
        {
            // Withdraw from neighbors
            withdrawSummaryRoute(route);

            // Remove from global routing table
            eigrpProcess.routingInstance->routingTable.removeEigrp(
                route->network.raw,
                route->mask,
                eigrpProcess.addressFamily,
                eigrpProcess.asNumber
            );

            // TODO remove discard route

            // Free route object
            delete route;

            // Remove from list
            it = list.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void EigrpInterface::restoreSummaryRoutes(const IPAddress& summaryNetwork, uint8_t summaryMask)
{
    std::shared_lock<std::shared_mutex> lock(configs->configsMutex);

    for (const auto& sr : configs->summaryRoutes)
    {
        if (sr.summary->network == summaryNetwork && sr.summary->mask == summaryMask)
        {
            advertiseSummaryRoute(sr.summary);

            // Reinstall discard route //TODO
//                 RoutingTable::Discard;
//                 discard.network == summaryRoute;
//                 discard.mask == summaryMask;
//                 discard.interfaceId = currentInterface->id;
//                 discard.protocol = "eigrp";
//                 discard.name = "summary-discard";

            //TODO remove discard route
            break;
        }
    }
}

EigrpConfigs::SummaryRoute* EigrpInterface::isSummarized(const uint8_t* network, uint8_t mask)
{
    std::shared_lock<std::shared_mutex> configsLock(configs->configsMutex);

    for (auto& sr : configs->summaryRoutes)
    {
        if (Functions::isSubnetOf(network, mask, sr.summary->network.raw, sr.summary->mask, eigrpProcess.addressFamily))
        {
            return &sr;
        }
    }
    return nullptr;
}
}
