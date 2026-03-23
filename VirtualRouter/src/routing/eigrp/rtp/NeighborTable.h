// NeighborTable.h

#ifndef EIGRP_NEIGHBOR_TABLE_H
#define EIGRP_NEIGHBOR_TABLE_H

#include <unordered_set>
#include <IPAddress.h>
#include "Neighbor.h"

namespace routing::eigrp
{
class EigrpInterface;

class NeighborTable
{
public:
    NeighborTable(EigrpInterface& iface);

    Neighbor* createNeighbor(const types::IPAddress& ipAddress, Neighbor::Version v = Neighbor::Version::UNKNOWN, bool unicast = false);
    void deleteNeighbor(const types::IPAddress& neighborIp, bool unicast);
    Neighbor* lookup(const types::IPAddress& neighborIp);
    std::vector<Neighbor*> lookupUnicast();
    size_t size();
    void onDown(Neighbor& neighbor);
    void resync();
    void startGracefulRestart(Neighbor& neighbor);
    void cancelAllHoldTimers();
    bool validatePTP(const types::IPAddress& neighborIp);
    void removeAllMulticast();
    void enableMulticast();
    void disableMulticast();
    
    // Neighbor management
    std::map<types::IPAddress, Neighbor> neighbors; ///< Map of neighbor IPs to their information.
    std::unordered_set<types::IPAddress> unicast;

    EigrpInterface& iface;
};
} // namespace routing

#endif // EIGRP_NEIGHBOR_TABLE
