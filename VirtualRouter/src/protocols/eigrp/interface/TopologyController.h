// TopologyController.h

#ifndef EIGRP_TOPOLOGY_CONTROLLER_H
#define EIGRP_TOPOLOGY_CONTROLLER_H

#include <cstdint>
#include <vector>
#include <IPAddress.h>
#include <unordered_map>

namespace Eigrp
{
class EigrpInterface;
class DuelEngine;
class Neighbor;
class NeighborTable;

struct RouteInfo;
struct TopologyEntry;
struct ReceivedRoute;

class TopologyController
{
public:
    TopologyController(NeighborTable& ntable, DuelEngine& duel, EigrpInterface& iface);

    uint64_t getLocalMetric();
    std::vector<const RouteInfo*> filterAdvertisableRoutes(const std::vector<const RouteInfo*> routes);
    void onNeighborDown(Neighbor& neighbor);

    std::vector<const RouteInfo*> getAllRoutes();
    std::unordered_map<IPPrefix, TopologyEntry*>& getTopologies();
    void refreshSuppression(std::vector<TopologyEntry*>& entries);
    void processReceivedRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor, uint32_t recvSeq);
    void processSIAReply(Neighbor& neighbor, uint32_t seq);
    void markRouteUnreachable(RouteInfo& route, const IPAddress& neighborIp, TopologyEntry& entry);
    TopologyEntry* findEntry(const IPPrefix& prefix);
    TopologyEntry& ensure(const IPPrefix& prefix);

private:
    NeighborTable& ntable;
    DuelEngine& duel;
    EigrpInterface& iface;
};
}

#endif // EIGRP_TOPOLOGY_CONTROLLER_H
