// BgpNeighborTable.h

#ifndef BGP_NEIGHBOR_TABLE_H
#define BGP_NEIGHBOR_TABLE_H

#include <unordered_map>
#include <cstdint>

#include "bgp/BgpTypes.hpp"

struct IPAddress;

namespace BGP
{
class BgpProcess;
class Neighbor;

class NeighborTable
{
public:
    NeighborTable(BgpProcess& proc);

    void syncNeighbors();

    Neighbor* createNeighbor(const NeighborKey& key);
    void deleteNeighbor(const NeighborKey& key);
    Neighbor* lookup(const NeighborKey& key);
    const Neighbor* lookup(const IPAddress& ipAddress) const;
    Neighbor* lookup(uint32_t rid);
    const Neighbor* lookup(uint32_t rid) const;

    bool activatePeer(const IPAddress& nbr, uint32_t peer);
    bool deactivatePeer(uint32_t peer);

    void cancelAllHoldTimers();


private:
    std::unordered_map<IPAddress, Neighbor> neighbors;
    std::unordered_map<uint32_t, Neighbor*> peers;

    BgpProcess& process;
};
}

#endif // BGP_NEIGHBOR_TABLE_H
