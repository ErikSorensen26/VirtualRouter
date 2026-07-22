// TopologyTable.cpp

#include "TopologyTable.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "DuelEngine.h"

namespace routing::eigrp
{
TopologyTable::TopologyTable(Eigrp& process) : process(process) {}

TopologyTable::~TopologyTable() {}

RouteInfo& TopologyTable::addRouteUpdate(const ReceivedRoute& route, const Neighbor* neighbor, TopologyEntry& entry)
{
    // Create or update the topology table entry
    types::IPAddress neighborIp = neighbor ? neighbor->ipAddress : route.nextHop;
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
            routeEntry.routeInfo.setFlag(ReceivedRoute::RouteFlags::WITHDRAWL);
    }

    return routeEntry;
}

std::pair<TopologyEntry*, RouteInfo*> TopologyTable::findPair(const types::IPPrefix& prefix, const types::IPAddress& neighbor)
{
    if (auto it = topologyEntries.find(prefix); it != topologyEntries.end())
        if (auto rit = it->second.routesBySource.find(neighbor); rit != it->second.routesBySource.end())
            return {&it->second, &rit->second};
    return {nullptr, nullptr};
}

TopologyEntry& TopologyTable::ensure(const types::IPPrefix& prefix)
{
    auto [it, inserted] = topologyEntries.emplace(prefix, TopologyEntry{});
    if (inserted)
    {
        it->second.prefix = prefix;
        process.getAggregator().addSummary(it->second);
    }
    return it->second;
}

TopologyEntry* TopologyTable::find(const types::IPPrefix& prefix)
{
    if (auto it = topologyEntries.find(prefix); it != topologyEntries.end())
        return &it->second;
    return nullptr;
}

void TopologyTable::markRouteUnreachable(RouteInfo& route, const types::IPAddress& neighborIp, TopologyEntry& entry)
{
    route.routeInfo.feasibleDistance = std::numeric_limits<uint64_t>::max();
    route.routeInfo.delay = std::numeric_limits<uint64_t>::max();
    route.isFeasibleSuccessor = false;
    route.isSuccessor = false;
    route.notFeasible = true;

    route.routeInfo.setFlag(ReceivedRoute::RouteFlags::WITHDRAWL);

    entry.feasibleSuccessors.erase(
        std::remove(entry.feasibleSuccessors.begin(), entry.feasibleSuccessors.end(), neighborIp),
        entry.feasibleSuccessors.end());

    entry.successors.erase(
        std::remove(entry.successors.begin(), entry.successors.end(), neighborIp),
        entry.successors.end());
}

void TopologyTable::pruneExpired()
{
    auto now = std::chrono::steady_clock::now();
    for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
    {
        if (it->second.valid && it->second.valid < now)
            it = topologyEntries.erase(it);
        else
            it++;
    }
}

void TopologyTable::pruneNeighbor(const types::IPAddress& neighborIp)
{
    for (auto& [_, entry] : topologyEntries)
    {
        if (entry.state == TopologyEntry::State::ACTIVE)
            continue;

        if (auto it = entry.routesBySource.find(neighborIp); it != entry.routesBySource.end())
        {
            entry.feasibleSuccessors.erase(
                std::remove(entry.feasibleSuccessors.begin(), entry.feasibleSuccessors.end(), neighborIp),
                entry.feasibleSuccessors.end());
            entry.successors.erase(
                std::remove(entry.successors.begin(), entry.successors.end(), neighborIp),
                entry.successors.end());
            entry.routesBySource.erase(it);
        }
    }

    pruneExpired();
}
} // namespace routing
