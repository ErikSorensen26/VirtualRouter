// EigrpTopology.h

#ifndef EIGRP_TOPOLOGY_H
#define EIGRP_TOPOLOGY_H

#include <IPAddress.hpp>
#include <EigrpTypes.hpp>
#include <TopologyTable.h>
#include <DuelEngine.h>

namespace Eigrp
{
class EigrpInterface;
class Eigrp;

class EigrpTopology
{
public:
    EigrpTopology(Eigrp& base);

    void recalculateAll();
    void pruneStaleRoutes();
    std::unordered_map<IPPrefix, TopologyEntry*>& entries();

    Eigrp& getBase() { return base; }

private:

    // Topology Table
    DuelEngine duel;
    Eigrp& base;
};
}

#endif // EIGRP_TOPOLOGY_H
