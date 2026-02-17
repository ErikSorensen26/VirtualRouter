// OspfNeighborTable.cpp

#include <unordered_set>
#include <IPAddress.hpp>

#include "Neighbor.h"
#include "NeighborTable.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
NeighborTable::NeighborTable(BgpProcess& proc)
    : process(proc) {}

void NeighborTable::syncNeighbors()
{
    auto& configs = process.getConfigs();

    std::unordered_set<IPAddress> unseen;

    // Fill unseen with all current neighbors
    for (const auto& [addr, _] : neighbors)
        unseen.insert(addr);

    auto& neighborList = configs.get<Config::Bgp::NEIGHBOR>().get();
    for (const auto& [ip, _] : neighborList)
    {
        if (!unseen.contains(ip))
        {
            createNeighbor(ip);
        }
        else
        {
            unseen.erase(ip);
        }
    }

    for (const auto& nbr : unseen)
    {
        deleteNeighbor(nbr);
    }
}

Neighbor* NeighborTable::createNeighbor(const IPAddress& ipAddress)
{
    // TODO 
}

void NeighborTable::deleteNeighbor(const IPAddress& ipAddress)
{
    // TODO
}

Neighbor* NeighborTable::lookup(const IPAddress& ipAddress)
{
    auto it = neighbors.find(ipAddress);
    if (it != neighbors.end())
        return &it->second;
    return nullptr;
}

const Neighbor* NeighborTable::lookup(const IPAddress& ipAddress) const
{
    auto it = neighbors.find(ipAddress);
    if (it != neighbors.end())
        return &it->second;
    return nullptr;
}

void NeighborTable::cancelAllHoldTimers()
{
    // TODO
}
}
