// TopologyTable.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "DuelEngine.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/rtp/NeighborTable.h"

namespace Eigrp
{
DuelEngine::DuelEngine(Eigrp& process) : base(process), topologyTable(process), tmgr(process, process.getScheduler()) {}

bool DuelEngine::setSuppression(TopologyEntry* entry, uint32_t key)
{
    auto it = entry->suppression.find(key);
    if (it == entry->suppression.end())
        return false;
    else if (!it->second.isSuppressed())
    {
        entry->suppression.erase(key);
        return false;
    }
    else return true;
}

void DuelEngine::refreshSuppression(std::vector<TopologyEntry*>& entries, EigrpInterface* iface)
{
    updateSuccessors(entries);
    bool resync;

    // Check for restart (Non active routes need to be withdrawn)
    for (auto& entry : entries)
    {
        if (setSuppression(entry, iface->interfaceKey))
            resync = true;
    }

    if (resync)
    {
        iface->getNTable().resync();
    }
}

std::vector<const RouteInfo*> DuelEngine::findBestRoutes(const IPPrefix& prefix)
{
    auto* entry = topologyTable.find(prefix);
    if (!entry || entry->routesBySource.empty()) return {};

    std::vector<const RouteInfo*> routes;
    for (const auto& neighbor : entry->successors)
        routes.push_back(&entry->routesBySource.at(neighbor));
    return routes;
}

const RouteInfo* DuelEngine::findBestRoute(const IPPrefix& prefix)
{
    auto* entry = topologyTable.find(prefix);
    if (!entry || entry->routesBySource.empty() || entry->successors.empty()) return nullptr;

    auto it = entry->routesBySource.find(entry->successors[0]);
    if (it == entry->routesBySource.end()) return nullptr;
    return &it->second;
}

void DuelEngine::recalculateAllRoutes()
{
    for (auto [_, top] : topologyTable.entries())
    {
        if (!top) continue;
        recalculateSuccessors(top);
    }
}

void DuelEngine::updateSuccessors(std::vector<TopologyEntry*>& entries)
{
    std::vector<TopologyEntry*> activeEntries;

    for (auto& entry : entries)
    {
        if (!recalculateSuccessors(entry))
        {
            activeEntries.push_back(entry);
        }
    }

    for (auto& entry : entries)
        base.getAggregator().updateSummary(*entry);
    
    base.routeManager.synchronizeRoutes(entries);

    if (!activeEntries.empty())
        setActive(activeEntries);
}

bool DuelEngine::recalculateSuccessors(TopologyEntry* entry)
{
    if (entry->routesBySource.empty()) return false;
    EigrpConfigs::TrafficShareMode trafMode = base.getGlobalConfigMgr().getTrafficMode();
    uint8_t variance = base.getGlobalConfigMgr().getVariance();

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();

    IPAddress bestNeighbor = entry->bestNeighbor;

    for (auto& route : entry->routesBySource)
    {
        route.second.isFeasibleSuccessor = false;
        route.second.isSuccessor = false;
    }

    entry->successors.clear();
    entry->feasibleSuccessors.clear();

    for (const auto& [nbr, route] : entry->routesBySource)
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

    for (auto& [nbr, route] : entry->routesBySource)
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

    for (auto route : entry->routesBySource)
        route.second.routeInfo.clearFlag(ReceivedRoute::RouteFlags::ACTIVE);

    if (entry->successors.empty())
    {
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

    bool changed = false;

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();
    IPAddress bestNeighbor = (base.getAF() == AddressFamily::IPv4) ? IPAddress(uint32_t(0)) : IPAddress(__uint128_t(0));

    for (auto& [nbr, route] : entry->routesBySource)
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
    std::vector<const RouteInfo*> reversePoisens;
    uint8_t maxHops = base.getGlobalConfigMgr().getMaxHops();
    for (auto& newRoute : newRoutes)
    {
        if (newRoute.hopCount >= maxHops || newRoute.reportedDistance > newRoute.feasibleDistance) continue;
        auto& entry = topologyTable.ensure(newRoute.prefix);
        bool reversePoisen = entry.routesBySource.empty();
        topologyTable.addRouteUpdate(newRoute, &neighbor, entry);

        if (entry.state == TopologyEntry::State::ACTIVE)
            processReceivedActiveRoute(newRoute, neighbor);
        else
            updates.push_back(&entry);

        if (reversePoisen)
            reversePoisens.push_back(&entry.routesBySource.at(neighbor.ipAddress));
    }

    if (!reversePoisens.empty())
        neighbor.getIface().getRtp().sendPoisenedUpdate(nullptr, reversePoisens); //TODO

    updateSuccessors(updates);
}

void DuelEngine::processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, const Neighbor& nbr)
{
    for (const auto& route : routes)
        processReceivedActiveRoute(route, nbr);
}

void DuelEngine::processReceivedQueryRoutes(std::vector<ReceivedRoute>& queriedRoutes, Neighbor& nbr, uint32_t recvSeq)
{
    std::vector<TopologyEntry*> toActivate;
    std::vector<const RouteInfo*> replies;

    bool isStub = base.getGlobalConfigMgr().stubEnabled();

    for (auto& route : queriedRoutes)
    {
        auto& entry = topologyTable.ensure(route.prefix);
        auto& updatedRoute = topologyTable.addRouteUpdate(route, &nbr, entry);

        if (recalculateSuccessors(&entry))
        {
            auto feasible = findBestRoute(route.prefix);
            if (feasible)
            {
                replies.push_back(feasible);
                continue;
            }
        }

        if (isStub)
            replies.push_back(&updatedRoute);
        else
            toActivate.push_back(&entry);
    }

    if (!replies.empty())
        nbr.getIface().getRtp().sendReply(nbr, replies);

    if (!toActivate.empty())
        setActive(toActivate, &recvSeq);
}

void DuelEngine::setActive(std::vector<TopologyEntry*>& entries, const uint32_t* seq)
{
    std::vector<ActiveRoute*> routes = {};
    {
        for (auto& entry : entries)
        {
            if (entry->routesBySource.count(entry->bestNeighbor) == 0 ||
                entry->state == TopologyEntry::State::ACTIVE)
                continue;

            base.routeManager.withdrawRoute(entry->prefix);

            IPAddress failedNeighbor = entry->bestNeighbor;

            // Create new active route
            ActiveRoute& ar = activeRoutes[entry->prefix];
            ar.originNeighbor = failedNeighbor;
            ar.activePrefix = entry->prefix;

            if (entry->routesBySource.count(failedNeighbor))
            {
                ar.originRoute = &entry->routesBySource.at(failedNeighbor);
                topologyTable.markRouteUnreachable(*ar.originRoute, failedNeighbor, *entry);
            }
            else
            {
                ar.originRoute = nullptr;
            }

            entry->state = TopologyEntry::State::ACTIVE;

            for (auto& route : entry->routesBySource)
                route.second.routeInfo.setFlag(ReceivedRoute::RouteFlags::ACTIVE);

            routes.push_back(&ar);

            if (seq)
                ar.remoteSources.insert({failedNeighbor, *seq});
        }
    }

    if (activeRoutes.empty())
        return;

    std::unordered_map<EigrpInterface*, std::vector<ActiveRoute*>> multicastBuckets;
    auto& allNeighbors = base.allNeighbors;
    for (auto& ar : routes)
    {
        IPAddress& origin = ar->originNeighbor;

        for (auto& [nbrIp, neighbor] : allNeighbors)
        {
            if (nbrIp == origin) continue;

            if (neighbor->isStub.load(std::memory_order_relaxed))
                continue;

            if (neighbor->unicast)
            {
                auto& query = ar->pendingQueries[nbrIp];
                query.route = ar;
                tmgr.startSIATimer(query, *neighbor);

                // Send unicast query
                std::vector<OutgoingQuery*> qs = { &query };
                neighbor->getIface().getRtp().sendUnicastQuery(*neighbor, qs);
            }
            else
            {
                EigrpInterface* iface = &neighbor->getIface();
                multicastBuckets[iface].push_back(ar);

                auto& query = ar->pendingQueries[nbrIp];
                query.route = ar;
                tmgr.startSIATimer(query, *neighbor);
            }
        }
    }

    for (auto& [iface, rts] : multicastBuckets)
    {
        iface->getRtp().sendQuery(rts);
    }

    for (auto& ar : routes)
    {
        if (ar->pendingQueries.empty())
            concludeActive(*ar);
    }
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

void DuelEngine::removeActiveNeighbor(const IPAddress& neighborIp)
{
    for (auto it = activeRoutes.begin(); it != activeRoutes.end();)
    {
        auto next = std::next(it);
        ActiveRoute& route = it->second;

        if (route.pendingQueries.erase(neighborIp) && route.pendingQueries.empty())
            concludeActive(route);

        it = next;
    }
}

void DuelEngine::handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor)
{
    ActiveRoute* route = nullptr;
    bool empty = false;
    {
        if (query.siaAttempts < 4)
        {
            neighbor.getIface().getRtp().sendSIAQuery(neighbor, {&query});
            query.siaAttempts++;
            tmgr.startSIATimer(query, neighbor);
            return;
        }
        route = query.route;
        route->pendingQueries.erase(neighbor.ipAddress);
        empty = route->pendingQueries.empty();
    }

    neighbor.getIface().getNTable().onDown(neighbor);
    if (empty)
        concludeActive(*route);
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

    for (const auto& src : activeRoute.remoteSources)
    {
        auto it = base.allNeighbors.find(src.first);
        if (it == base.allNeighbors.end()) continue;
        auto feasibleRoutes = findBestRoutes(activeRoute.activePrefix);
        it->second->getIface().getRtp().sendReply(*it->second, feasibleRoutes);
    }

    activeRoutes.erase(activeRoute.activePrefix);
    base.routeManager.synchronizeRoute(*entry);
}
}
