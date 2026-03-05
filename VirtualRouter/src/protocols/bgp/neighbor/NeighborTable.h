// BgpNeighborTable.h

#ifndef BGP_NEIGHBOR_TABLE_H
#define BGP_NEIGHBOR_TABLE_H

#include <unordered_map>
#include <cstdint>

#include "bgp/neighbor/Neighbor.h"

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

    Neighbor* createNeighbor(const IPAddress& ipAddress);

    void deleteNeighbor(const IPAddress& ipAddress);

    Neighbor* lookup(const IPAddress& ipAddress);
    const Neighbor* lookup(const IPAddress& ipAddress) const;

    Neighbor* lookup(uint32_t rid);
    const Neighbor* lookup(uint32_t rid) const;

    bool activatePeer(const IPAddress& nbr, uint32_t peer);
    bool deactivatePeer(uint32_t peer);

    void cancelAllHoldTimers();

    template <typename F>
    void forEachNeighbor(F&& fn)
    {
        for (auto& [addr, nbr] : neighbors)
            fn(nbr);
    }

    template <typename F>
    void forEachNeighbor(F&& fn) const
    {
        for (const auto& [addr, nbr] : neighbors)
            fn(nbr);
    }

    template <typename F>
    void forEachPeer(F&& fn)
    {
        for (auto& [addr, nbr] : peers)
            fn(*nbr);
    }

    template <typename F>
    void forEachPeer(F&& fn) const
    {
        for (const auto& [addr, nbr] : peers)
            fn(*nbr);
    }

private:
    std::unordered_map<IPAddress, Neighbor> neighbors;
    std::unordered_map<uint32_t, Neighbor*> peers;

    BgpProcess& process;
};
}

#endif // BGP_NEIGHBOR_TABLE_H
