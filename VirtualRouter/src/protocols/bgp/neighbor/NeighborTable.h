// BgpNeighborTable.h

#ifndef BGP_NEIGHBOR_TABLE_H
#define BGP_NEIGHBOR_TABLE_H

#include <unordered_map>

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

    void cancelAllHoldTimers();

    std::unordered_map<IPAddress, Neighbor> neighbors;

private:
    BgpProcess& process;
};
}

#endif // BGP_NEIGHBOR_TABLE_H
