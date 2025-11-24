// TopologyTable.cpp

#include "DuelEngine.h"
#include <Eigrp.h>
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <Global.h>
#include <TimeManager.h>
#include <NeighborTable.h>

namespace Eigrp
{
DuelEngine::DuelEngine(Eigrp& process) : base(process), topologyTable(process), tmgr(process, process.routingInstance->global.timeManager) {}

DuelEngine::~DuelEngine() {}

std::vector<const RouteInfo*> DuelEngine::findBestRoutes(const IPPrefix& prefix)
{
    auto* entry = topologyTable.find(prefix);
    std::lock_guard<std::mutex> lock(topologyTable.tableMutex);
    if (!entry || entry->routesByNeighbor.empty()) return {};

    std::vector<const RouteInfo*> routes;
    for (const auto& neighbor : entry->successors)
        routes.push_back(&entry->routesByNeighbor.at(neighbor));
    return routes;
}

const RouteInfo* DuelEngine::findBestRoute(const IPPrefix& prefix)
{
    auto* entry = topologyTable.find(prefix);
    std::lock_guard<std::mutex> lock(topologyTable.tableMutex);
    if (!entry || entry->routesByNeighbor.empty() || entry->successors.empty()) return nullptr;

    auto it = entry->routesByNeighbor.find(entry->successors[0]);
    if (it == entry->routesByNeighbor.end()) return nullptr;
    return &it->second;
}

void DuelEngine::recalculateAllRoutes()
{
    for (auto [_, top] : topologyTable.entries())
    {
        if (!top) continue;
        std::lock_guard<std::mutex> lock(top->entryMutex);
        recalculateSuccessors(top);
    }
}

void DuelEngine::updateSuccessors(std::vector<TopologyEntry*>& entries, const IPAddress& updatedNeighbor)
{
    std::vector<IPPrefix> activeEntries;

    for (auto& entry : entries)
    {
        std::lock_guard<std::mutex> lock(entry->entryMutex);

        if (!entry->routesByNeighbor.count(updatedNeighbor))
            continue;
        auto& route = entry->routesByNeighbor.at(updatedNeighbor);
        auto& bestFD = entry->bestFD;
        auto& bestAD = entry->bestAD;
        auto& bestNeighbor = entry->bestNeighbor;

        bool recalcAll = (route.routeInfo.reportedDistance < bestFD);

        if (entry->successors.empty() || !entry->routesByNeighbor.count(bestNeighbor))
            recalcAll = true;
        else if (route.routeInfo.reportedDistance < bestFD)
            recalcAll = true;
        else if (route.routeInfo.feasibleDistance == bestFD && route.routeInfo.adminDistance < bestAD)
            recalcAll = true;
        else if (route.notFeasible && bestNeighbor == updatedNeighbor)
            recalcAll = true;

        if (recalcAll) 
        {
            if (!recalculateSuccessors(entry))
            {
                activeEntries.push_back(entry->prefix);
                continue;
            }
        }
    }

    for (auto& entry : entries)
        base.getAggregator().updateSummary(*entry);
    
    base.routeManager.synchronizeRoutes(entries);

    if (!activeEntries.empty())
        setActive(activeEntries, updatedNeighbor);
}

bool DuelEngine::recalculateSuccessors(TopologyEntry* entry)
{
    EigrpConfigs::TrafficShareMode trafMode = base.getGlobalConfigMgr().getTrafficMode();
    uint8_t variance = base.getGlobalConfigMgr().getVariance();

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();
    IPAddress bestNeighbor = IPAddress(base.getAF());

    for (auto& route : entry->routesByNeighbor)
    {
        route.second.isFeasibleSuccessor = false;
        route.second.isSuccessor = false;
    }

    entry->successors.clear();
    entry->feasibleSuccessors.clear();

    for (const auto& [nbr, route] : entry->routesByNeighbor)
    {
        if (route.routeInfo.feasibleDistance == std::numeric_limits<uint64_t>::max())
            continue;

        if (route.routeInfo.feasibleDistance < bestFD)
        {
            bestFD = route.routeInfo.feasibleDistance;
            bestAD = route.routeInfo.adminDistance;
            bestNeighbor = nbr;
        }
    }

    entry->bestFD = bestFD;
    entry->bestAD = bestAD;
    entry->bestNeighbor = bestNeighbor;

    for (auto& [nbr, route] : entry->routesByNeighbor)
    {
        if (route.routeInfo.feasibleDistance == std::numeric_limits<uint64_t>::max())
            continue;

        route.isFeasibleSuccessor = (route.routeInfo.reportedDistance < bestFD);
        if (route.isFeasibleSuccessor)
            entry->feasibleSuccessors.push_back(nbr);

        route.isSuccessor = route.routeInfo.feasibleDistance <= (bestFD * variance);

        if (route.isSuccessor)
            entry->successors.push_back(nbr);
    }

    if (base.isNamed())
        for (auto route : entry->routesByNeighbor)
            route.second.routeInfo.wide.clearFlag(ReceivedRoute::Wide::WideFlags::ACTIVE);

    if (entry->successors.empty())
    {
        entry->state = TopologyEntry::State::POISENED;
        entry->bestFD = std::numeric_limits<uint64_t>::max();
        return false;
    }

    if (trafMode == EigrpConfigs::TrafficShareMode::Minimum && !entry->successors.empty())
        entry->successors = {entry->successors.front()};

    entry->state = TopologyEntry::State::PASSIVE;

    return true;
}

bool DuelEngine::recalculateDistances(TopologyEntry* entry, uint64_t localMetric)
{
    if (!entry) return false;

    std::lock_guard<std::mutex> lock(entry->entryMutex);
    bool changed = false;

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();
    IPAddress bestNeighbor = IPAddress(base.getAF());

    for (auto& [nbr, route] : entry->routesByNeighbor)
    {
        route.routeInfo.feasibleDistance = route.routeInfo.reportedDistance + localMetric;

        if (route.routeInfo.feasibleDistance < bestFD ||
            (route.routeInfo.feasibleDistance == bestFD && route.routeInfo.adminDistance < bestFD))
        {
            bestFD = route.routeInfo.feasibleDistance;
            bestAD = route.routeInfo.adminDistance;
            bestNeighbor = nbr;
        }
    }

    if (bestFD != entry->bestFD || bestNeighbor != entry->bestNeighbor)
        changed = true;

    if (changed)
    {
        entry->bestFD = bestFD;
        entry->bestAD = bestAD;
        entry->bestNeighbor = bestNeighbor;
    }

    return changed;
}

void DuelEngine::processReceivedRoutes(std::vector<ReceivedRoute>& newRoutes, const Neighbor& neighbor)
{
    std::vector<TopologyEntry*> updates;
    uint8_t maxHops = base.getGlobalConfigMgr().getMaxHops();
    for (auto& newRoute : newRoutes)
    {
        if (newRoute.hopCount >= maxHops || newRoute.reportedDistance > newRoute.feasibleDistance) continue;
        auto& entry = topologyTable.ensure(newRoute.prefix);
        topologyTable.addRouteUpdate(newRoute, &neighbor, entry);

        if (entry.state == TopologyEntry::State::ACTIVE)
            processReceivedActiveRoute(newRoute, neighbor);
        else
            updates.push_back(&entry);
    }

    updateSuccessors(updates, neighbor.ipAddress);
}

void DuelEngine::processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, const Neighbor& nbr)
{
    for (const auto& route : routes)
        processReceivedActiveRoute(route, nbr);
}

void DuelEngine::processReceivedQueryRoutes(std::vector<ReceivedRoute>& queriedRoutes, Neighbor& nbr, uint32_t recvSeq)
{
    std::vector<IPPrefix> toActivate;

    for (auto& route : queriedRoutes)
    {
        auto* entry = topologyTable.find(route.prefix);
        if (!entry)
            continue;

        if (!entry->feasibleSuccessors.empty())
        {
            auto feasible = findBestRoutes(route.prefix);
            nbr.getIface().getRtp().sendReply(nbr, feasible, recvSeq);
        }
        else
        {
            toActivate.push_back(route.prefix);
        }
    }

    if (!toActivate.empty())
        setActive(toActivate, nbr.ipAddress, &recvSeq);
}

void DuelEngine::setActive(const std::vector<IPPrefix>& prefixes, const IPAddress& failedNeighbor, const uint32_t* seq)
{
    std::vector<ActiveRoute*> routes = {};
    base.routeManager.withdrawRoutes(prefixes);
    {
        std::lock_guard<std::mutex> lock(activeMutex);
        for (const auto& prefix : prefixes)
        {
            if (auto entry = topologyTable.findPair(prefix, failedNeighbor); entry.first && entry.second)
            {
                if (entry.first->state == TopologyEntry::State::ACTIVE)
                    continue;

                // Create new active route
                ActiveRoute& ar = activeRoutes[prefix];
                ar.originNeighbor = failedNeighbor;
                ar.activePrefix = prefix;
                ar.originRoute = entry.second;

                topologyTable.markRouteUnreachable(*entry.second, failedNeighbor, *entry.first);
                entry.first->state = TopologyEntry::State::ACTIVE;
                if (base.isNamed())
                    for (auto& route : entry.first->routesByNeighbor)
                        route.second.routeInfo.wide.setFlag(ReceivedRoute::Wide::WideFlags::ACTIVE);
                routes.push_back(&ar);

                if (seq)
                    ar.remoteSources.insert({failedNeighbor, *seq});
            }
        }
    }

    if (seq || activeRoutes.empty())
        return;

    std::unordered_set<EigrpInterface*> multicastQueryInterfaces;
    auto& allNeighbors = base.allNeighbors;
    for (const auto& [ip, neighbor] : allNeighbors)
    {
        if (ip != failedNeighbor && !neighbor->isStub.load(std::memory_order_relaxed))
        {
            if (neighbor->unicast)
            {
                std::vector<OutgoingQuery*> queries;
                queries.reserve(routes.size());
                for (const auto& ar : routes)
                {
                    auto& query = ar->pendingQueries[neighbor->ipAddress];
                    query.route = ar;
                    tmgr.startSIATimer(query, *neighbor);
                    queries.push_back(&query);
                }
                neighbor->getIface().getRtp().sendUnicastQuery(*neighbor, queries);
            }
            else
            {
                // Add query info and start SIA timer
                EigrpInterface* neighborIface = &neighbor->getIface();
                multicastQueryInterfaces.insert(neighborIface);

                for (const auto& ar : routes)
                {
                    auto& query = ar->pendingQueries[ip];
                    query.route = ar;
                    tmgr.startSIATimer(query, *neighbor);
                }
            }
        }
    }

    for (auto& mult : multicastQueryInterfaces)
    {
        mult->getRtp().sendQuery(routes);
    }

    for (auto& route : routes)
        if (route->pendingQueries.empty())
            concludeActive(*route);
}

void DuelEngine::processReceivedActiveRoute(const ReceivedRoute& recvRoute, const Neighbor& neighbor)
{
    auto ar = activeRoutes.find(recvRoute.prefix);
    if (ar == activeRoutes.end()) return; // Not active 
    auto& activeRoute = ar->second;

    if (neighbor.ipAddress == ar->second.originNeighbor)
    {
        ar->second.possibleRoutes.push_back({neighbor.ipAddress, recvRoute});
    }
    else if (auto rit = ar->second.pendingQueries.find(neighbor.ipAddress); rit != ar->second.pendingQueries.end())
    {
        tmgr.cancelSIATimer(rit->second);
        ar->second.possibleRoutes.push_back({neighbor.ipAddress, recvRoute});
        activeRoute.pendingQueries.erase(neighbor.ipAddress);
    }

    // Check if completed
    if (activeRoute.pendingQueries.empty())
    {
        concludeActive(ar->second);
    }
}

void DuelEngine::processSIAReply(Neighbor& neighbor, uint32_t seq)
{
    std::lock_guard<std::mutex> lock(activeMutex);
    for (auto& [_, route] : activeRoutes)
    {
        if (auto it = route.pendingQueries.find(neighbor.ipAddress); it != route.pendingQueries.end())
        {
            if (it->second.siaSequence == seq)
            {
                tmgr.cancelSIATimer(it->second);
                tmgr.startSIATimer(it->second, neighbor);
            }
        }
    }
}

void DuelEngine::handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor)
{
    std::lock_guard<std::mutex> lock(activeMutex);
    if (query.siaAttempts < 4)
    {
        neighbor.getIface().getRtp().sendSIAQuery(neighbor, {&query});
        query.siaAttempts++;
        query.lastHeard = std::chrono::steady_clock::now();
        tmgr.startSIATimer(query, neighbor);
        return;
    }

    auto& route = *query.route;
    route.pendingQueries.erase(neighbor.ipAddress);
    neighbor.getIface().getNTable().onDown(neighbor);
    if (route.pendingQueries.empty())
        concludeActive(route);
}

void DuelEngine::concludeActive(ActiveRoute& activeRoute)
{
    auto* entry = topologyTable.find(activeRoute.activePrefix);
    if (!entry) return;

    for (auto& [neighbor, route] : activeRoute.possibleRoutes)
    {
        auto it = base.allNeighbors.find(neighbor);
        if (it == base.allNeighbors.end()) continue;
        topologyTable.addRouteUpdate(route, it->second, *entry);
    }

    if (!recalculateSuccessors(entry))
    {
        entry->state = TopologyEntry::State::POISENED;
        entry->valid = std::chrono::steady_clock::now() + std::chrono::seconds(base.getGlobalConfigMgr().getDelTimer());
    }
    else
        entry->state = TopologyEntry::State::PASSIVE;

    base.routeManager.synchronizeRoute(*entry);

    for (const auto& src : activeRoute.remoteSources)
    {
        auto it = base.allNeighbors.find(src.first);
        if (it == base.allNeighbors.end()) continue;
        auto feasibleRoutes = findBestRoutes(activeRoute.activePrefix);
        it->second->getIface().getRtp().sendReply(*it->second, feasibleRoutes, src.second);
    }

    activeRoutes.erase(activeRoute.activePrefix);
}
}
