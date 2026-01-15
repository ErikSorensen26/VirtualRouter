// OspfNeighborTable.h

#ifndef OSPF_NEIGHBOR_TABLE_H
#define OSPF_NEIGHBOR_TABLE_H

#include <shared_mutex>
#include <map>
#include <NeighborTable.h>

struct IPAddress;

namespace OSPF
{
class Neighbor;
class NeighborTable
{
public:
    //void setState(Neighbor& neighbor, Neighbor::State newState);
    Neighbor* createNeighbor(uint32_t rid, const IPAddress& ipAddress, bool unicast = false);
    void deleteNeighbor(uint32_t rid, bool unicast);
    Neighbor* lookup(uint32_t rid);
    const Neighbor* lookup(uint32_t rid) const;
    bool isUnicast(uint32_t rid);

    std::optional<size_t> addNeighborList(uint8_t* buf, size_t maxSize);
    mutable std::shared_mutex mu;
    std::map<uint32_t, Neighbor> neighbors;

private:
    std::unordered_set<uint32_t> unicast;
};
}

#endif // OSPF_NEIGHBOR_TABLE_H
