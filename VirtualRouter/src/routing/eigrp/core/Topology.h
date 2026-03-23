// EigrpTopology.h

#ifndef EIGRP_TOPOLOGY_H
#define EIGRP_TOPOLOGY_H

#include <IPAddress.h>

#include "eigrp/topology/DuelEngine.h"

class Internal_EigrpTest;

namespace routing::eigrp
{
class EigrpInterface;
class Eigrp;

class EigrpTopology
{
public:
    friend class Internal_EigrpTest;
    friend class EigrpInterface;
    EigrpTopology(Eigrp& base);

    void recalculateAll();
    void pruneStaleRoutes();
    void synchronizeConnected(EigrpInterface& iface);
    void clearConnected(EigrpInterface& iface);
    void handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor);
    std::unordered_map<types::IPPrefix, TopologyEntry>& entries();

    Eigrp& getBase() { return base; }

private:

    // Topology Table
    DuelEngine duel;
    Eigrp& base;
};
} // namespace routing

#endif // EIGRP_TOPOLOGY_H

