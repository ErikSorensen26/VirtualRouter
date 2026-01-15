// OspfNeighborTable.cpp

#include "OspfNeighborTable.h"
#include "OspfNeighbor.h"
#include <LsaKey.hpp>

namespace OSPF
{
Neighbor* NeighborTable::createNeighbor(uint32_t rid, const IPAddress& ipAddress, bool unicast)
{

}
void NeighborTable::deleteNeighbor(uint32_t rid, bool unicast)
{

}

Neighbor* NeighborTable::lookup(uint32_t rid)
{
    std::shared_lock<std::shared_mutex> lock(mu);
    auto it = neighbors.find(rid);
    if (it != neighbors.end())
        return &it->second;
    return nullptr;
}

const Neighbor* NeighborTable::lookup(uint32_t rid) const
{
    std::shared_lock<std::shared_mutex> lock(mu);
    auto it = neighbors.find(rid);
    if (it != neighbors.end())
        return &it->second;
    return nullptr;
}

std::optional<size_t> NeighborTable::addNeighborList(uint8_t* buf, size_t maxSize)
{
    std::shared_lock<std::shared_mutex> lock(mu);
    size_t siz = neighbors.size();
    if (maxSize > siz * 4) return std::nullopt;
    size_t off = 0;
    for (auto& [rid, _] : neighbors)
    {
        writeU32(buf + off, rid);
        off += 4;
    }
    return off;
}
}
