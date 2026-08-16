// TopologyTable.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "DualEngine.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/rtp/NeighborTable.h"

namespace routing::eigrp
{
DualEngine::DualEngine(Eigrp& process) : process(process), topologyTable(process), tmgr(process, process.getScheduler()) {}

bool DualEngine::isRouteAdvertised(const uint8_t* network, uint8_t mask)
{
    types::IPPrefix prefix(network, mask, process.addressFamily, true);
    auto* entry = topologyTable.find(prefix);
    return entry != nullptr && !entry->successors.empty();
}

bool DualEngine::setSuppression(TopologyEntry* entry, uint32_t key)
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

void DualEngine::refreshSuppression(std::vector<TopologyEntry*>& entries, EigrpInterface* iface)
{
    updateSuccessors(entries);
    bool resync = false;

    // Check for restart (Non active routes need to be withdrawn)
    for (auto& entry : entries)
    {
        if (setSuppression(entry, iface->interfaceKey.getId()))
            resync = true;
    }

    if (resync)
    {
        iface->ntable.resync();
    }
}

const RouteInfo* DualEngine::findBestRoute(const types::IPPrefix& prefix)
{
    auto* entry = topologyTable.find(prefix);
    if (!entry || entry->routesBySource.empty() || entry->successors.empty()) return nullptr;

    auto it = entry->routesBySource.find(entry->successors[0]);
    if (it == entry->routesBySource.end()) return nullptr;
    return &it->second;
}

void DualEngine::recalculateAllRoutes()
{
    for (auto& [_, top] : topologyTable.entries())
        recalculateSuccessors(&top);
}

void DualEngine::updateSuccessors(std::vector<TopologyEntry*>& entries)
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
        process.getAggregator().updateSummary(*entry);
    
    process.routeManager.synchronizeRoutes(entries);

    if (!activeEntries.empty())
        setActive(activeEntries);
}

bool DualEngine::recalculateSuccessors(TopologyEntry* entry)
{
    if (entry->routesBySource.empty()) return false;
    config::eigrp::TrafficShareMode trafMode = process.getConfigs().get<config::Eigrp::TRAFFIC_SHARE>().load();
    uint8_t variance = process.getConfigs().get<config::Eigrp::VARIANCE>().load();

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();

    types::IPAddress bestNeighbor = entry->bestNeighbor;

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

    for (auto& [_, route] : entry->routesBySource)
        route.routeInfo.clearFlag(ReceivedRoute::RouteFlags::ACTIVE);

    if (entry->successors.empty())
    {
        entry->bestFD = std::numeric_limits<uint64_t>::max();
        return false;
    }

    if (trafMode == config::eigrp::TrafficShareMode::MINIMUM && !entry->successors.empty())
        entry->successors = {entry->successors.front()};

    entry->state = TopologyEntry::State::PASSIVE;

    return true;
}

bool DualEngine::recalculateDistances(TopologyEntry* entry, uint64_t localMetric)
{
    if (!entry) return false;

    bool changed = false;

    uint64_t bestFD = std::numeric_limits<uint64_t>::max();
    uint8_t bestAD = std::numeric_limits<uint8_t>::max();
    types::IPAddress bestNeighbor = (process.addressFamily == types::AddressFamily::IPv4) ? types::IPAddress(uint32_t(0)) : types::IPAddress(__uint128_t(0));

    for (auto& [nbr, route] : entry->routesBySource)
    {
        route.routeInfo.feasibleDistance = route.routeInfo.reportedDistance + localMetric;

        if (route.routeInfo.feasibleDistance < bestFD ||
            (route.routeInfo.feasibleDistance == bestFD && route.routeInfo.adminDistance < bestAD))
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

void DualEngine::processReceivedRoutes(std::vector<ReceivedRoute>& newRoutes, const Neighbor& neighbor)
{
    std::vector<TopologyEntry*> updates;
    std::vector<const RouteInfo*> reversePoisens;
    uint8_t maxHops = process.getConfigs().get<config::Eigrp::MAX_HOPS>().load();
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
        neighbor.getIface().rtp.sendPoisenedUpdate(nullptr, reversePoisens);

    updateSuccessors(updates);
}

void DualEngine::processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, const Neighbor& nbr)
{
    for (const auto& route : routes)
        processReceivedActiveRoute(route, nbr);
}

void DualEngine::processReceivedQueryRoutes(std::vector<ReceivedRoute>& queriedRoutes, Neighbor& nbr, uint32_t recvSeq)
{
    std::vector<TopologyEntry*> toActivate;
    std::vector<const RouteInfo*> replies;

    auto stubField = process.getConfigs().get<config::Eigrp::STUB>();
    bool isStub = stubField.hasValue() && stubField.load().any();

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
        nbr.getIface().rtp.sendReply(nbr, replies);

    if (!toActivate.empty())
        setActive(toActivate, &recvSeq);
}

void DualEngine::setActive(std::vector<TopologyEntry*>& entries, const uint32_t* seq)
{
    std::vector<ActiveRoute*> routes = {};
    {
        for (auto& entry : entries)
        {
            types::IPAddress failedNeighbor = entry->bestNeighbor;

            if (!entry->routesBySource.contains(failedNeighbor) ||
                entry->state == TopologyEntry::State::ACTIVE)
                continue;

            process.routeManager.withdrawRoute(entry->prefix);

            // Create new active route
            ActiveRoute& ar = activeRoutes[entry->prefix];
            ar.originNeighbor = failedNeighbor;
            ar.activePrefix = entry->prefix;

            ar.originRoute = &entry->routesBySource.at(failedNeighbor);
            topologyTable.markRouteUnreachable(*ar.originRoute, failedNeighbor, *entry);

            entry->state = TopologyEntry::State::ACTIVE;

            for (auto& route : entry->routesBySource)
                route.second.routeInfo.setFlag(ReceivedRoute::RouteFlags::ACTIVE);

            routes.push_back(&ar);

            if (seq)
                ar.remoteSources.insert({failedNeighbor, *seq});
        }
    }

    if (routes.empty())
        return;

    std::unordered_map<EigrpInterface*, std::vector<ActiveRoute*>> multicastBuckets;
    auto& allNeighbors = process.allNeighbors;
    for (auto& ar : routes)
    {
        types::IPAddress& origin = ar->originNeighbor;

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
                neighbor->getIface().rtp.sendUnicastQuery(*neighbor, qs);
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
        iface->rtp.sendQuery(rts);
    }

    for (auto& ar : routes)
    {
        if (ar->pendingQueries.empty())
            concludeActive(*ar);
    }
}

void DualEngine::processReceivedActiveRoute(const ReceivedRoute& recvRoute, const Neighbor& neighbor)
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

void DualEngine::processSIAReply(Neighbor& neighbor, uint32_t seq)
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

void DualEngine::removeActiveNeighbor(const types::IPAddress& neighborIp)
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

void DualEngine::handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor)
{
    ActiveRoute* route = nullptr;
    bool empty = false;
    {
        if (query.siaAttempts < 4)
        {
            neighbor.getIface().rtp.sendSIAQuery(neighbor, {&query});
            query.siaAttempts++;
            tmgr.startSIATimer(query, neighbor);
            return;
        }
        route = query.route;
        route->pendingQueries.erase(neighbor.ipAddress);
        empty = route->pendingQueries.empty();
    }

    neighbor.getIface().ntable.onDown(neighbor);
    if (empty)
        concludeActive(*route);
}

void DualEngine::concludeActive(ActiveRoute& activeRoute)
{
    auto* entry = topologyTable.find(activeRoute.activePrefix);
    if (!entry) return;

    for (auto& [neighbor, route] : activeRoute.possibleRoutes)
    {
        auto it = process.allNeighbors.find(neighbor);
        if (it == process.allNeighbors.end()) continue;
        topologyTable.addRouteUpdate(route, it->second, *entry);
    }

    if (!recalculateSuccessors(entry))
    {
        entry->state = TopologyEntry::State::POISENED;
        entry->valid = std::chrono::steady_clock::now() + std::chrono::seconds(120); // 3x default hold time (3 * 40s)
    }
    else
        entry->state = TopologyEntry::State::PASSIVE;

    if (!activeRoute.remoteSources.empty())
    {
        std::vector<const RouteInfo*> replies;
        if (auto* replyEntry = topologyTable.find(activeRoute.activePrefix))
        {
            for (const auto& neighbor : replyEntry->successors)
            {
                if (auto rit = replyEntry->routesBySource.find(neighbor); rit != replyEntry->routesBySource.end())
                    replies.push_back(&rit->second);
            }
        }
        for (const auto& src : activeRoute.remoteSources)
        {
            auto it = process.allNeighbors.find(src.first);
            if (it == process.allNeighbors.end()) continue;
            it->second->getIface().rtp.sendReply(*it->second, replies);
        }
    }

    activeRoutes.erase(activeRoute.activePrefix);
    process.routeManager.synchronizeRoute(*entry);
}
} // namespace routing
