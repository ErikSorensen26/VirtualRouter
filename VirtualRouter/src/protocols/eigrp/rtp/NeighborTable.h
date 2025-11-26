// NeighborTable.h

#ifndef EIGRP_NEIGHBOR_TABLE_H
#define EIGRP_NEIGHBOR_TABLE_H

#include <shared_mutex>
#include <IPAddress.hpp>
#include "Neighbor.h"

namespace Eigrp
{
class EigrpInterface;

class NeighborTable
{
public:
    NeighborTable(EigrpInterface& iface);

    void setState(Neighbor& neighbor, Neighbor::State newState);
    Neighbor* createNeighbor(const IPAddress& ipAddress, Neighbor::Version v = Neighbor::Version::UNKNOWN, const uint8_t* macAddress = nullptr);
    void deleteNeighbor(const IPAddress& neighborIp, bool unicast);
    Neighbor* lookup(const IPAddress& neighborIp);
    std::vector<Neighbor*> lookupUnicast();
    size_t size();
    void onDown(Neighbor& neighbor);
    void resync();
    void startGracefulRestart(Neighbor& neighbor);
    void cancelAllHoldTimers();
    bool validatePTP(const IPAddress& neighborIp);
    void removeAllMulticast(); // TODO
    void enableMulticast();
    void disableMulticast();
    
    // Neighbor management
    std::shared_mutex neighborMutex; ///< Shared mutex for neighbor operations.
    std::map<IPAddress, Neighbor> neighbors; ///< Map of neighbor IPs to their information.
    std::unordered_set<IPAddress> unicast;

    EigrpInterface& iface;
};
}

#endif // EIGRP_NEIGHBOR_TABLE
