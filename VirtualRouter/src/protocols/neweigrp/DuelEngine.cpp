// TopologyTable.cpp

#include "TopologyTable.h"
#include <Eigrp.h>
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <Global.h>
#include <TimeManager.h>

namespace Protocol
{
TopologyTable::DuelEngine(Eigrp& process) : eigrpProcess(process) {}

TopologyTable::~DuelEngine() {}

bool DuelEngine::isRouteAdvertised(const uint8_t* network, uint8_t mask)
{
    std::shared_lock<std::shared_mutex> lock(neighborMutex);
    for (const auto& [address, neighbor] : neighbors)
    {
        IPPrefix key(network, mask, eigrpProcess.addressFamily);
        auto routeIt = neighbor->advertisedRoutes.find(key);
        if (routeIt == neighbor->advertisedRoutes.end())
        {
            return false;
        }
    }
    return true;
}

void EigrpInterface::checkSuppression()
{
    if (!isSupressed) return;

    auto now = std::chrono::steady_clock::now();
    if (now >= supressedUntil)
    {
        if (restartCounter >= eigrpProcess.configs.dampeningRestartCount)
        {
            if (eigrpProcess.configs.dampeningWarnings)
                std::cout << ""; //TODO warning output
            return;
        }

        // Add delay before nect re-evaluation
        supressedUntil = now + std::chrono::seconds(eigrpProcess.configs.dampeningRestart.load(std::memory_order_relaxed));
        isSupressed = false;
        routeChangeTimes.clear();

        if (eigrpProcess.configs.dampeningWarnings.load(std::memory_order_relaxed))
            std::cout << ""; //TODO warning output

        eigrpProcess.routingInstance->global.timeManager.addTimer(
            now + std::chrono::seconds(eigrpProcess.configs.dampeningResetTime.load(std::memory_order_relaxed)),
            [&]() { checkSuppressionStatus(); }
        );
    }
}

void EigrpInterface::handleRouteChange()
{
    uint32_t maxPrefix = eigrpProcess.configs.maximumPrefix.load(std::memory_order_relaxed);
    if (maxPrefix == 0) return;

    prefixCount.fetch_add(1, std::memory_order_acquire);
    if (prefixCount.load(std::memory_order_relaxed) > maxPrefix)
    {
        isSupressed = true;
        restartCounter++;
        routeChangeTimes.clear();

        if (eigrpProcess.configs.dampeningWarnings.load(std::memory_order_relaxed))
            std::cout << ""; //TODO warning output

        eigrpProcess.routingInstance->global.timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.dampeningResetTime.load(std::memory_order_relaxed)),
            [&]() { checkSuppressionStatus(); }
        );
    }
}

std::optional<TopologyTable::RouteInfo> TopologyTable::findBestRoute(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    auto it = topologyEntries.find(prefix);
    if (it == topologyEntries.end())
    {
        return std::nullopt;
    }

    const auto& entry = it->second;

    if (entry->successors.empty())
    {
        return std::nullopt;
    }

    // Return the route information for the best successor
    const IPAddress& bestNeighbor = entry->successors.front();
    return entry->routesByNeighbor.at(bestNeighbor);
}

void TopologyTable::recalculateFeasibleSuccessors(TopologyEntry* entry)
{
    // Initialize the best feasible distance (FD) and the best administrative distance
    uint32_t bestFD = std::numeric_limits<uint32_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();

    // Clear current successors and feasible successor list
    entry->successors.clear();
    entry->feasibleSuccessors.clear();

    // Step 1: Find the best feasible distance (FD) and lowest administrative distance (AD)
    for (const auto& [neighbor, route] : entry->routesByNeighbor)
    {
        bestFD = std::min(bestFD, route.feasibleDistance);
        bestAD = std::min(bestAD, route.adminDistance);
    }

    // Step 2: make sure feasible routes are still present
    bool feasibleFound = false;
    for (auto& [neighbor, route] : entry->routesByNeighbor)
    {
        if (!route.notFeasible)
        {
            feasibleFound = true;
            break; // Found a feasible route
        }
    }
    if (!feasibleFound)
    {
        return; // No new feasible routes
    }

    // Step 3: Determin successors and feasible successors
    for (auto& [neighbor, route] : entry->routesByNeighbor)
    {
        // Feasibility Condition: Reported Distance < Best Feasible Distance
        route.isFeasibleSuccessor = (route.reportedDistance < bestFD);

        // If the route is a feasible successor, add it to the feasible successors list
        if (route.isFeasibleSuccessor)
        {
            entry->feasibleSuccessors.push_back(neighbor);
        }

        // Check if the route meets the Successor Condition for being a successor
        bool withinVariance = (route.feasibleDistance <= bestFD * eigrpProcess.configMgr.configs.variance.load(std::memory_order_relaxed));

        // Route must have FD within the variance threshold and AD equal to best AD to be a successor
        if (withinVariance && route.adminDistance == bestAD)
        {
            route.isSuccessor = true; // Mark as a successor
            entry->successors.push_back(neighbor); // Add to successor list
        }
        else
        {
            route.isSuccessor = false; // Mark false if not a successor
        }

        if (!route.isSuccessor && !route.isFeasibleSuccessor)
        {
            route.notFeasible = true;
        }
        else
        {
            route.notFeasible = false;
        }
    }

    // Step 3: Apply Traffic-Share mode
    if (eigrpProcess.configMgr.configs.trafficShareMode.load(std::memory_order_relaxed) == EigrpConfigs::TrafficShareMode::Minimum)
    {
        // Only keep the route with the lowest FD
        if (!entry->successors.empty())
        {
            const IPAddress& bestNeighbor = entry->successors.front();
            entry->successors = {bestNeighbor}; // Keep only the best
        }
    }
}

void TopologyTable::handleRouteFailure(const IPPrefix& prefix, const IPAddress& failedNeighborIp)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    auto it = topologyEntries.find(prefix);
    if (it == topologyEntries.end())
    {
        return;
    }

    auto& entry = it->second;

    // Remove the failed neighbor's route
    entry->routesByNeighbor.erase(failedNeighborIp);

    // If no remaining neighbors, remove route completely
    if (entry->routesByNeighbor.empty())
    {
        topologyEntries.erase(it);
        return;
    }

    // Recalculate successors and feasible successors
    updateSuccessorAndFeasibleSuccessors(entry);
}

void TopologyTable::markRouteAsPassive(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto it = topologyEntries.find(prefix);
    if (it != topologyEntries.end())            // Default constructor
    {
        auto entry = it->second;
        entry->isActive = false;

        // Cancel Active timer if running
        if (entry->activeTimerId != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(entry->activeTimerId);
            entry->activeTimerId = 0;
        }
    }
}
}
