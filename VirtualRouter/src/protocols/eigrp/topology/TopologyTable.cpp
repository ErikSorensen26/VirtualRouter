// TopologyTable.cpp

#include "TopologyTable.h"
#include <Eigrp.h>
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <Global.h>
#include "DuelEngine.h"
#include <TimeManager.h>

namespace Eigrp
{
TopologyTable::TopologyTable(Eigrp& process) : eigrpProcess(process) {}

TopologyTable::~TopologyTable() {}

std::vector<const RouteInfo*> TopologyTable::getSuccessors(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    
    auto* entry = find(prefix);
    if (!entry) return {};

    std::vector<const RouteInfo*> successors;
    for (const auto& [_, route] : entry->routesBySource)
        if (route.isSuccessor)
            successors.push_back(&route);
    return successors;
}

std::vector<const RouteInfo*> TopologyTable::getAllRoutes()
{
    std::vector<const RouteInfo*> routesToSend;
    std::lock_guard<std::mutex> lock(tableMutex);

    for (const auto& [prefix, entryPtr] : topologyEntries)
    {
        if (!entryPtr)
            continue;
        std::lock_guard<std::mutex> entryLock(entryPtr->entryMutex);

        const IPAddress& bestNeighbor = entryPtr->bestNeighbor;
        auto it = entryPtr->routesBySource.find(bestNeighbor);
        if (it == entryPtr->routesBySource.end())
            continue;

        const RouteInfo& route = it->second;
        routesToSend.push_back(&route);
    }
    return routesToSend;
}

RouteInfo& TopologyTable::addRouteUpdate(const ReceivedRoute& route, const Neighbor* neighbor, TopologyEntry& entry)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    // Create or update the topology table entry
    IPAddress neighborIp = neighbor ? neighbor->ipAddress : route.nextHop;
    auto it = entry.routesBySource.find(neighborIp);
    if (it == entry.routesBySource.end())
    {
        ReceivedRoute cp = route;
        entry.routesBySource.emplace(neighborIp, RouteInfo{cp});
    }

    auto& routeEntry = entry.routesBySource.at(neighborIp);
    routeEntry.routeInfo = std::move(route);
    routeEntry.lastUpdate = std::chrono::steady_clock::now();
    routeEntry.topology = &entry;

    if (route.delay == std::numeric_limits<uint64_t>::max())
    {
        routeEntry.isFeasibleSuccessor = false;
        routeEntry.isSuccessor = false;
        routeEntry.notFeasible = true;

        if (routeEntry.routeInfo.wide.isWide)
            routeEntry.routeInfo.wide.setFlag(ReceivedRoute::Wide::WideFlags::WITHDRAWL);
    }

    return routeEntry;
}

std::pair<TopologyEntry*, RouteInfo*> TopologyTable::findPair(const IPPrefix& prefix, const IPAddress& neighbor)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    if (auto it = topologyEntries.find(prefix); it != topologyEntries.end())
        if (auto rit = it->second->routesBySource.find(neighbor); rit != it->second->routesBySource.end())
            return {it->second, &rit->second};
    return {nullptr, nullptr};
}

TopologyEntry& TopologyTable::ensure(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    if (auto it = topologyEntries.find(prefix); it != topologyEntries.end())
        return *it->second;
    TopologyEntry* entry = new TopologyEntry();
    entry->prefix = prefix;
    eigrpProcess.getAggregator().addSummary(*entry);
    topologyEntries[prefix] = entry;
    return *entry;
}

TopologyEntry* TopologyTable::find(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    if (auto it = topologyEntries.find(prefix); it != topologyEntries.end())
    {
        return it->second;
    }
    return nullptr;
}

void TopologyTable::markRouteUnreachable(RouteInfo& route, const IPAddress& neighborIp, TopologyEntry& entry)
{
    std::lock_guard<std::mutex> lock(entry.entryMutex);

    route.routeInfo.feasibleDistance = std::numeric_limits<uint64_t>::max();
    route.routeInfo.delay = std::numeric_limits<uint64_t>::max();
    route.isFeasibleSuccessor = false;
    route.isSuccessor = false;
    route.notFeasible = true;

    if (eigrpProcess.isNamed())
        route.routeInfo.wide.setFlag(ReceivedRoute::Wide::WideFlags::WITHDRAWL);

    entry.feasibleSuccessors.erase(
        std::remove(entry.feasibleSuccessors.begin(), entry.feasibleSuccessors.end(), neighborIp),
        entry.feasibleSuccessors.end());

    entry.successors.erase(
        std::remove(entry.successors.begin(), entry.successors.end(), neighborIp),
        entry.successors.end());
}

void TopologyTable::pruneExpired()
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
    {
        if (it->second->valid && it->second->valid < now)
            it = topologyEntries.erase(it);
        else
            it++;
    }
}

void TopologyTable::pruneNeighbor(const IPAddress& neighborIp)
{
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        for (auto& [_, entry] : topologyEntries)
        {
            if (auto it = entry->routesBySource.find(neighborIp); it != entry->routesBySource.end())
            {
                entry->feasibleSuccessors.erase(
                    std::remove(entry->feasibleSuccessors.begin(), entry->feasibleSuccessors.end(), neighborIp),
                    entry->feasibleSuccessors.end());
                entry->successors.erase(
                    std::remove(entry->successors.begin(), entry->successors.end(), neighborIp),
                    entry->successors.end());
            }
        }
    }

    pruneExpired(); // Remove any empty destinations
}
}

