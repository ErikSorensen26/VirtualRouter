// DuelEngine.h

#ifndef EIGRP_DUEL_ENGINE_H
#define EIGRP_DUEL_ENGINE_H

#include <cstdint>
#include <set>
#include <IPAddress.hpp>
#include <TopologyTable.h>
#include <EigrpTypes.hpp>
#include "TimerManager.h"

class Internal_EigrpTest;

namespace Eigrp
{
class EigrpInterface;
class Eigrp;

struct ActiveRoute;

struct OutgoingQuery
{
    ActiveRoute* route;
    uint32_t siaSequence{0};
    uint32_t querySequence{0};
    uint32_t siaTimerId{0}, siaAttempts{0};
    std::chrono::steady_clock::time_point lastHeard;
};

struct ActiveRoute
{
    std::unordered_map<IPAddress, OutgoingQuery> pendingQueries;
    std::vector<std::pair<IPAddress, const ReceivedRoute&>> possibleRoutes;
    IPPrefix activePrefix;
    std::set<std::pair<IPAddress, uint32_t>> remoteSources;
    RouteInfo* originRoute = nullptr;
    IPAddress originNeighbor;
};

class DuelEngine
{
public:
    friend class ::Internal_EigrpTest;
    DuelEngine(Eigrp& process);

    ~DuelEngine();
    bool isRouteAdvertised(const uint8_t* network, uint8_t mask);
    std::vector<const RouteInfo*> findBestRoutes(const IPPrefix& prefix);
    const RouteInfo* findBestRoute(const IPPrefix& prefix);

    void handleReceivedQuery(const std::vector<ReceivedRoute>& routes, Neighbor& neighbor);

    void setActive(const std::vector<IPPrefix>& prefixes, const IPAddress& ipAddress, const uint32_t* seq = nullptr);
    void concludeActive(ActiveRoute& route);
    void setPassive(const IPPrefix& prefix);
    void setPoisened(const IPPrefix& prefix);

    void handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor);
    void processSIAReply(Neighbor& neighbor, uint32_t seqNum);
    void processReceivedRoutes(std::vector<ReceivedRoute>& newRoutes, const Neighbor& neighbor);
    void processReceivedActiveRoutes(std::vector<ReceivedRoute>& newRoute, const Neighbor& neighbor);
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& queriedRoutes, Neighbor& nbr, uint32_t recvSeq);

    void updateSuccessors(std::vector<TopologyEntry*>& entry, const IPAddress& neighborIp);

    void recalculateAllRoutes();

    Eigrp& base;
    TopologyTable topologyTable;

private:
    
    TimerManager tmgr;

    void processReceivedRoute(const ReceivedRoute& newRoute, const Neighbor& neighbor);
    void processReceivedActiveRoute(const ReceivedRoute& newRoute, const Neighbor& neighbor);
    bool recalculateSuccessors(TopologyEntry* entry);
    bool recalculateDistances(TopologyEntry* entry, uint64_t localMetric);

    // Lists
    std::set<std::pair<IPAddress, TopologyEntry*>> pendingUpdates;
    std::map<IPPrefix, ActiveRoute> activeRoutes; ///< Map of outstanding query IDs to neighbor IPs and timer IDs.
    std::mutex activeMutex;
};
}

#endif // DUEL_ENGINE_H
