// EigrpTopology.h

#ifndef EIGRP_TOPOLOGY_H
#define EIGRP_TOPOLOGY_H

#include <IPAddress.hpp>
#include <EigrpTypes.hpp>
#include <TopologyTable.h>
#include <DuelEngine.h>

class Internal_EigrpTest;

namespace Eigrp
{
class EigrpInterface;
class Eigrp;

class EigrpTopology
{
public:
    friend class ::Internal_EigrpTest;
    friend class EigrpInterface;
    EigrpTopology(Eigrp& base);

    void recalculateAll();
    void pruneStaleRoutes();
    void synchronizeConnected(EigrpInterface& iface);
    void clearConnected(EigrpInterface& iface);
    void handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor);
    std::unordered_map<IPPrefix, TopologyEntry*>& entries();

    Eigrp& getBase() { return base; }

private:

    // Topology Table
    DuelEngine duel;
    Eigrp& base;
};
}

#endif // EIGRP_TOPOLOGY_H
