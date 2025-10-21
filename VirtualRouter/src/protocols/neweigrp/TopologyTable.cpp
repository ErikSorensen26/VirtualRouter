// TopologyTable.cpp

#include "TopologyTable.h"
#include <Eigrp.h>
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <Global.h>
#include <TimeManager.h>

namespace Protocol
{
TopologyTable::TopologyTable(Eigrp& process) : eigrpProcess(process) {}

TopologyTable::~TopologyTable() {}

std::vector<RoutingTable::Eigrp*> TopologyTable::getSuccessorsForRoute(const IPAddress& network, uint8_t mask)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    std::vector<RoutingTable::Eigrp*> successors;

    IPPrefix key(network.raw, mask, eigrpProcess.addressFamily);
    auto it = topologyEntries.find(key);
    if (it == topologyEntries.end()) return {};

    TopologyEntry* entry = it->second;

    updateSuccessorAndFeasibleSuccessors(entry);

    if (entry->successors.empty())
        return {};

    for (const auto& neighbor : entry->successors)
    {
        const auto& route = entry->routesByNeighbor.at(neighbor);
        if (route.feasibleDistance == std::numeric_limits<uint32_t>::max())
            continue;

        // Construct a routing table entry
        RoutingTable::Eigrp* routingEntry = new RoutingTable::Eigrp(entry->routesByNeighbor[neighbor].eigrpInterface->interfaceKey);
        routingEntry->network = entry->destination;
        routingEntry->mask = entry->prefixLength;
        routingEntry->nextHop = neighbor;
        routingEntry->feasibleDistance = route.feasibleDistance;
        routingEntry->reportedDistance = route.reportedDistance;
        routingEntry->routeType = route.routeType;

        if (route.routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            routingEntry->adminDistance = eigrpProcess.configMgr.configs.externalAdminDistance.load(std::memory_order_relaxed);
        else
            routingEntry->adminDistance = eigrpProcess.configMgr.configs.adminDistance.load(std::memory_order_relaxed);

        successors.push_back(routingEntry);
    }


    for (const auto& [destination, topEntry] : topologyEntries)
    {
        if (!topEntry->successors.empty())
        {
        }
    }

    return successors;
}

void TopologyTable::addOrUpdateRoute(const IPAddress& neighborIp, const IPAddress& destination, uint8_t prefixLength, const RouteInfo &routeInfo)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    // Create or update the topology table entry
    IPPrefix key(destination.raw, prefixLength, eigrpProcess.addressFamily);

    auto entryIt = topologyEntries.find(key);
    TopologyEntry* entry = nullptr;
    if (entryIt == topologyEntries.end())
    {
        entry = new TopologyEntry();
        entry->destination = destination;
        entry->prefixLength = prefixLength;
        topologyEntries[key] = entry;
    }
    else
    {
        entry = entryIt->second;
    }

    {
        if (routeInfo.reportedDistance > routeInfo.feasibleDistance) return;
        entry->routesByNeighbor[neighborIp] = routeInfo;
        entry->routesByNeighbor[neighborIp].lastUpdate = std::chrono::steady_clock::now();
    }

    // Recalculate successors and feasible successors
    updateSuccessorAndFeasibleSuccessors(entry);
}

void TopologyTable::erase(const IPAddress& neighborIp)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
    {
        it->second->routesByNeighbor.erase(neighborIp);
        if (it->second->routesByNeighbor.empty())
        {
            it = topologyEntries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

TopologyTable::TopologyEntry* TopologyTable::find(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    auto it = topologyEntries.find(prefix);
    if (it == topologyEntries.end())
    {
        return nullptr;
    }

    return it->second;
}

bool TopologyTable::removeRoute(const IPPrefix& prefix, const IPAddress& neighbor)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto entryIt = topologyEntries.find(prefix);
    if (entryIt == topologyEntries.end())
    {
        return false; // Entry does not exist
    }
    entryIt->second->routesByNeighbor.erase(neighbor);
    if (entryIt->second->routesByNeighbor.empty())
    {
        delete entryIt->second;
        topologyEntries.erase(prefix);
    }
    return true;
}

void TopologyTable::pruneExpired()
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
    {
        auto entry = it->second;
        // Iterate through all neighbors for this destination
        for (auto neighborIt = entry->routesByNeighbor.begin(); neighborIt != entry->routesByNeighbor.end();)
        {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - neighborIt->second.lastUpdate).count();
            // Prune if the age exceeds the stale threshold
            if (age < staleThreshold) // Check against the stale threshold
            {
                neighborIt = entry->routesByNeighbor.erase(neighborIt); // Remove stale route
            }
            else
            {
                ++neighborIt;
            }
        }

        // Remove the entry if no neighbors remain
        if (entry->routesByNeighbor.empty())
        {
            it = topologyEntries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TopologyTable::handleNeighborDown(const IPAddress& neighborIp)
{
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        for (auto &entry : topologyEntries)
        {
            entry.second->routesByNeighbor.erase(neighborIp); // Remove routes from this neighbor
        }
    }

    pruneStaleRoutes(); // Remove any empty destinations
}
}

