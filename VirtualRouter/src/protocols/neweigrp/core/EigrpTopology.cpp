// EigrpTopology.cpp

#include "EigrpTopology.h"

namespace Eigrp
{
EigrpTopology::EigrpTopology(Eigrp& base) : duel(base), base(base) {}

void EigrpTopology::pruneStaleRoutes()
{
    duel.topologyTable.pruneExpired();
}

std::unordered_map<IPPrefix, TopologyEntry*>& EigrpTopology::entries()
{
    return duel.topologyTable.entries();
}

void EigrpTopology::recalculateAll()
{
    duel.recalculateAllRoutes();
}
}
