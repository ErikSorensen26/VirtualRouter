// OspfNeighborTable.cpp

#include "OspfNeighborTable.h"
#include "OspfNeighbor.h"
#include <LsaKey.hpp>
#include <OspfInterface.h>

namespace OSPF
{
NeighborTable::NeighborTable(OspfInterface& iface)
    : iface(iface) {}

void NeighborTable::syncUnicast()
{
    auto& ifaceConfigs = iface.getConfigs();
    auto ntype = ifaceConfigs.get<Config::OspfInterface::NETWORK>().load();

    std::unique_lock<std::shared_mutex> lk(mu);

    if (ntype == NetworkType::POINT_TO_MULTIPOINT || ntype == NetworkType::NON_BROADCAST)
    {
        std::unordered_set<IPAddress> unicastNbrs;

        // Snap shot of all current neighbors
        for (const auto& [ip, _] : unicast)
            unicastNbrs.insert(ip);

        // Update configs of all unicast neighbors
        ifaceConfigs.get<Config::OspfInterface::NEIGHBOR>().withRead([&](const auto& nbrs) {
            for (const auto& [ip, cost, dbfilter, pollIntv, priority] : nbrs)
            {
                unicastNbrs.erase(ip);
                auto it = unicast.emplace(ip);
                auto& nbr = it.first->second;
                nbr.cost = cost;
                nbr.databaseFilter = dbfilter.has_value() ? dbfilter.value() : false;
                nbr.pollInterval = pollIntv.has_value() ? pollIntv.value() : 120;
                nbr.priority = priority.has_value() ? priority.value() : 0;
            }
        });

        // Erase left over neighbors
        for (const auto& ip : unicastNbrs)
        {
            unicastNbrs.erase(ip);
            for (auto it = neighbors.begin(); it != neighbors.end();)
            {
                if (it->second.ipAddress == ip && it->second.unicast)
                {
                    it = neighbors.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }
    else
    {
        // Clear all unicast neighbors

        unicast.clear();
        
        for (auto it = neighbors.begin(); it != neighbors.end();)
        {
            if (it->second.unicast)
            {
                it = neighbors.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void NeighborTable::clearUnicast()
{

}

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

void NeighborTable::cancelAllInactiveTimers()
{
    std::shared_lock<std::shared_mutex> lk(mu);
    for (auto& [_, nbr] : neighbors)
        iface.getTimers().cancleInactiveTimer(nbr);
}
}
