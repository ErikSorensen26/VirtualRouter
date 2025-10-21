// EigrpInterfaceTopology.cpp

namespace Protocol
{
void EigrpInterface::updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const std::vector<EigrpConfigs::RoutingUpdate>& routes)
{
    // Validate neighbor
    if (!neighbor) return; // invalid neighbor

    std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes;
    std::vector<RoutingTable::Eigrp*> withdrawnRoutes;

    for (const auto& route : routes)
    {
        if (route.withdraw)
        {
            eigrpProcess.topologyTable->removeRoute({route.route->network.raw, route.route->mask, eigrpProcess.addressFamily}, neighborIp);
            auto* existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            if (existingRoute && existingRoute->nextHop == neighborIp)
            {
                withdrawnRoutes.push_back(existingRoute);
            }
        }
        else
        {
            if (route.route->reportedDistance > route.route->feasibleDistance) continue; // Violation

            if (auto* summary = isRouteSummarized(route.route->network.raw, route.route->mask))
            {
                if (route.route->bandwidth < summary->summary->bandwidth)
                {
                    summary->summary->bandwidth = route.route->bandwidth;
                }
                if (route.route->delay < summary->summary->delay)
                {
                    summary->summary->delay = route.route->delay;
                }
            }

            // Access the topology table and update it with new routes
            TopologyTable::RouteInfo routeInfo;
            routeInfo.eigrpInterface = this;
            routeInfo.bandwidthMetric = route.route->bandwidth;
            routeInfo.delayMetric = route.route->delay;
            routeInfo.feasibleDistance = route.route->feasibleDistance;
            routeInfo.reportedDistance = route.route->reportedDistance;
            {
                std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                routeInfo.nextHop = neighbor->ipAddress;
            }
            routeInfo.hopCount = route.route->hopCount;
            routeInfo.isSuccessor = false;
            routeInfo.isFeasibleSuccessor = false;
            routeInfo.routeType = route.route->routeType;

            // Add or update the route in the topology table
            eigrpProcess.topologyTable->addOrUpdateRoute(neighborIp, route.route->network, route.route->mask, routeInfo);

            // Fetch the updated topology table
            auto bestRouteEntry = eigrpProcess.topologyTable->getEntryForRoute({route.route->network.raw, route.route->mask, eigrpProcess.addressFamily});
            if (!bestRouteEntry)
            {
                continue;
            }

            auto successorIt = std::find_if(
                bestRouteEntry->routesByNeighbor.begin(),
                bestRouteEntry->routesByNeighbor.end(),
                [](const auto& pair) {return pair.second.isSuccessor;});

            if (successorIt == bestRouteEntry->routesByNeighbor.end())
            {
                continue;
            }

            // Prepare the administrative distance based on the route type
            RoutingTable::Eigrp* newRoute = route.route;
            newRoute->nextHop = successorIt->second.nextHop;
            newRoute->feasibleDistance = successorIt->second.feasibleDistance;

            // Set administrative distance  based on route type
            if (newRoute->routeType == RoutingTable::Eigrp::RouteType::INTERNAL)
            {
                newRoute->adminDistance = eigrpProcess.configs.adminDistance.load(std::memory_order_relaxed);
            }
            else if (newRoute->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            {
                newRoute->adminDistance = eigrpProcess.configs.externalAdminDistance.load(std::memory_order_relaxed);
            }
            else if  (newRoute->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
            {
                newRoute->adminDistance = eigrpProcess.configs.adminDistance.load(std::memory_order_relaxed);
            }
            else 
            {
                continue; // Skip unknown route type
            }

            // Check for existing routes and determine if an update is needed
            auto existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            if (!existingRoute) onPrefixLearned(); // New prefix learned
            bool routeChange = !existingRoute || (existingRoute->feasibleDistance != newRoute->feasibleDistance);

            if (routeChange)
            {
                eigrpProcess.routingInstance->routingTable.updateEigrp(newRoute, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                updatedRoutes.emplace_back(newRoute, false);
            }
        }
    }
    eigrpProcess.notifyRoutingChange(updatedRoutes);

    for (const auto& removedRoute : updatedRoutes)
    {
        if (!removedRoute.withdraw) continue;
        if (auto* summary = isRouteSummarized(removedRoute.route->network.raw, removedRoute.route->mask))
        {
            if (summary->summary->bandwidth == removedRoute.route->bandwidth &&
                summary->summary->delay == removedRoute.route->delay)
            {
                auto allEigrp = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess.addressFamily, eigrpProcess.asNumber);
                uint32_t lowDelay = std::numeric_limits<uint32_t>::max();
                uint32_t lowBandwidth = std::numeric_limits<uint32_t>::max();
                for (const auto& route : allEigrp)
                {
                    if (route->network == summary->summary->network && route->mask == summary->summary->mask)
                    {
                        lowDelay = std::min(lowDelay, route->delay);
                    }
                    if (route->network == summary->summary->network && route->mask == summary->summary->mask)
                    {
                        lowBandwidth = std::min(lowBandwidth, route->bandwidth);
                    }
                }

                summary->summary->bandwidth = lowBandwidth;
                summary->summary->delay = lowDelay;
            }
        }

        eigrpProcess.routingInstance->routingTable.removeEigrp(removedRoute.route->network.raw, removedRoute.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
        auto successors = eigrpProcess.topologyTable->getSuccessorsForRoute(removedRoute.route->network, removedRoute.route->mask);
        bool routeAdded = false;
        for (auto& route : successors)
        {
            if (routeAdded)
                eigrpProcess.routingInstance->routingTable.addEigrp(route, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            else
                eigrpProcess.routingInstance->routingTable.updateEigrp(route, eigrpProcess.addressFamily, eigrpProcess.asNumber);
        }
    }
}

void EigrpInterface::updateRoutingTableForDestination(const IPPrefix& prefix)
{
    auto entry = eigrpProcess.topologyTable->getEntryForRoute(prefix);
    if (!entry)
    {
        eigrpProcess.routingInstance->routingTable.removeEigrp(prefix.addr, prefix.prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
        return;
    }
    
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

void EigrpInterface::handleStubRouteUpdates()
{
    // Identify routes that should no longer be advertised
    std::vector<EigrpConfigs::RoutingUpdate> routesToWithdraw;
    std::shared_lock<std::shared_mutex> lock(neighborMutex);
    for (const auto& [address, neighbor] : neighbors)
    {
        std::shared_lock<std::shared_mutex> neighborDataLock(neighbor->neighborDataMutex);
        for (const auto& [routeKey, advertisedRoute] : neighbor->advertisedRoutes)
        {
            // Check if route has already been removed
            bool found = false;
            for (auto route : routesToWithdraw)
            {
                if (route.route->network == advertisedRoute.route->network && route.route->mask == advertisedRoute.route->mask)
                {
                    found = true;
                }
            }
            if (found)
            {
                continue;
            }

            // Check if route should be removed
            bool shouldAdvertise = false;

            if (eigrpProcess.isStub())
            {
                if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED && eigrpProcess.advertiseConnected())
                    shouldAdvertise = true;
                if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::STATIC && eigrpProcess.advertiseStatic())
                    shouldAdvertise = true;
                if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY && eigrpProcess.advertiseSummary())
                    shouldAdvertise = true;
                if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL &&  eigrpProcess.advertiseRedistributed())
                    shouldAdvertise = true;
            }
            else
            {
                shouldAdvertise = true;
            }

            if (!shouldAdvertise)
            {
                // Prepare to withdraw this route
                RoutingTable::Eigrp* withdrawRoute = advertisedRoute.route;
                withdrawRoute->metric = std::numeric_limits<uint32_t>::max();
                withdrawRoute->routeType = RoutingTable::Eigrp::RouteType::WITHDRAW;

                routesToWithdraw.push_back({withdrawRoute, true});
            }
        }
    }
    
    if (!routesToWithdraw.empty())
    {
        // Withdraw routes to all neighbors
        bool multicast = false;
        std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
        {
            std::shared_lock<std::shared_mutex> neighborLock(neighborMutex);
            for (const auto& [neighborIp, neighborInfo] : neighbors)
            {
                if (neighborInfo->unicast)
                {
                    if (!neighborInfo->isInit) continue;

                    // Send withdraw updates
                    unicastNeighbors.push_back(neighborInfo);
                }
                else
                {
                    multicast = true;
                }
            }
        }

        for (const auto& neighbor : unicastNeighbors)
        {
            sendUpdateToNeighbor(neighbor, routesToWithdraw, EigrpConfigs::UpdateType::PARTIAL);
        }
        if (multicast && configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            sendUpdateToNeighbor(nullptr, routesToWithdraw, EigrpConfigs::UpdateType::PARTIAL);
        }
    }
}

void EigrpInterface::recordRouteChange()
{
    if (!eigrpProcess.configs.dampening.load(std::memory_order_relaxed)) return;

    auto now = std::chrono::steady_clock::now();
    routeChangeTimes.push_back(now);

    // Drop old changes outside of interval
    while (!routeChangeTimes.empty() &&
           now - routeChangeTimes.front() > std::chrono::seconds(configs->dampeningInterval.load(std::memory_order_relaxed)))
    {
        routeChangeTimes.pop_front();
    }

    // Supress if too many changes
    if (!isSupressed && routeChangeTimes.size() >= configs->dampeningChange)
    {
        isSupressed.store(true, std::memory_order_release);
        supressedUntil = now + std::chrono::seconds(eigrpProcess.configs.dampeningResetTime.load(std::memory_order_relaxed));
        ++restartCounter;

        if (eigrpProcess.configs.dampeningWarnings)
            std::cout << ""; //TODO warning output

        eigrpProcess.routingInstance->global.timeManager.addTimer(
            supressedUntil,
            [&]() { checkSuppressionStatus(); }
        );
    }
}
}
