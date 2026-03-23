// TopologyController.h

#ifndef EIGRP_TOPOLOGY_CONTROLLER_H
#define EIGRP_TOPOLOGY_CONTROLLER_H

#include <cstdint>
#include <vector>
#include <IPAddress.h>
#include <unordered_map>

namespace routing::eigrp
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
    std::vector<const RouteInfo*> filterAdvertisableRoutes(const std::vector<const RouteInfo*>& routes);
    std::vector<const RouteInfo*> getAdvertisableRoutes();
    void onNeighborDown(Neighbor& neighbor);

    std::unordered_map<types::IPPrefix, TopologyEntry>& getTopologies();
    void refreshSuppression(std::vector<TopologyEntry*>& entries);
    void processReceivedRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor, uint32_t recvSeq);
    void processSIAReply(Neighbor& neighbor, uint32_t seq);
    void markRouteUnreachable(RouteInfo& route, const types::IPAddress& neighborIp, TopologyEntry& entry);
    TopologyEntry* findEntry(const types::IPPrefix& prefix);
    TopologyEntry& ensure(const types::IPPrefix& prefix);

private:
    NeighborTable& ntable;
    DuelEngine& duel;
    EigrpInterface& iface;
};
} // namespace routing

#endif // EIGRP_TOPOLOGY_CONTROLLER_H

