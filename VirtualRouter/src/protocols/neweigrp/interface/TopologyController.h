// TopologyController.h

#ifndef EIGRP_TOPOLOGY_CONTROLLER_H
#define EIGRP_TOPOLOGY_CONTROLLER_H

#include <cstdint>
#include <TopologyTable.h>
#include <Neighbor.h>

namespace Eigrp
{
class DuelEngine;
class Neighbor;
class NeighborTable;

class TopologyController
{
public:
    TopologyController(NeighborTable& ntable, DuelEngine& duel, EigrpInterface& iface);

    uint64_t getLocalMetric();
    std::vector<const RouteInfo*> filterAdvertisableRoutes(const std::vector<const RouteInfo*> routes);
    void onNeighborDown(const IPAddress& neighborIp);

    std::vector<const RouteInfo*> getAllRoutes();
    std::unordered_map<IPPrefix, TopologyEntry*>& getTopologies();
    void processReceivedRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor, uint32_t recvSeq);
    void processSIAReply(Neighbor& neighbor, uint32_t seq);
    TopologyEntry* findEntry(const IPPrefix& prefix);

private:
    NeighborTable& ntable;
    DuelEngine& duel;
    EigrpInterface& iface;
};
}

#endif // EIGRP_TOPOLOGY_CONTROLLER_H
