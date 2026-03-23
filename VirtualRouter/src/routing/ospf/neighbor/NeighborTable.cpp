// OspfNeighborTable.cpp

#include "Neighbor.h"
#include "NeighborTable.h"
#include "ospf/interface/OspfInterface.h"

namespace routing::ospf
{
NeighborTable::NeighborTable(OspfInterface& iface)
    : iface(iface) {}

void NeighborTable::syncUnicast()
{
    auto& ifaceConfigs = iface.getConfigs();
    auto ntype = ifaceConfigs.get<config::OspfInterface::NETWORK>().load();

    if (ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT || ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        std::unordered_set<types::IPAddress> unicastNbrs;

        // Snap shot of all current neighbors
        for (const auto& [ip, _] : unicast)
            unicastNbrs.insert(ip);

        using NeighborEntry = std::tuple<types::IPAddress, std::optional<uint16_t>, std::optional<bool>, std::optional<uint16_t>, std::optional<uint8_t>>;

        // Update configs of all unicast neighbors
        auto updateNeighbors = [&](const std::vector<NeighborEntry>& nbrs)
        {
            for (auto& [ip, cost, dbfilter, pollIntv, priority] : nbrs)
            {
                if (!iface.interfaceAddress.contains(ip))
                    continue;
                unicastNbrs.erase(ip);
                unicast.try_emplace(
                    ip, 
                    cost,
                    dbfilter.value_or(false),
                    pollIntv.value_or(120),
                    priority.value_or(0)
                );
            }
        };

        ifaceConfigs.get<config::OspfInterface::NEIGHBOR>().withRead([&](const std::vector<NeighborEntry>& nbrs) {
            updateNeighbors(nbrs);
        });

        iface.getArea().process().getConfigs().get<config::Ospf::NEIGHBORS>().withRead([&](const std::vector<NeighborEntry>& nbrs) {
            updateNeighbors(nbrs);
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
    unicast.clear();
    for (auto it = neighbors.begin(); it != neighbors.end();)
    {
        if (it->second.unicast)
            it = neighbors.erase(it);
        else
            ++it;
    }
}

Neighbor* NeighborTable::createNeighbor(uint32_t rid, const types::IPAddress& ipAddress, bool isUnicast)
{
    auto it = neighbors.find(rid);
    if (it != neighbors.end())
        return &it->second;

    // Neighbor constructor takes types::IPAddress& (non-const), so make a mutable copy
    types::IPAddress ip = ipAddress;
    auto [ins, ok] = neighbors.try_emplace(rid, iface, iface.getTimers(), rid, ip, isUnicast);
    return &ins->second;
}

void NeighborTable::deleteNeighbor(uint32_t rid, bool /*unicast*/)
{
    neighbors.erase(rid);
}

Neighbor* NeighborTable::lookup(uint32_t rid)
{
    auto it = neighbors.find(rid);
    if (it != neighbors.end())
        return &it->second;
    return nullptr;
}

const Neighbor* NeighborTable::lookup(uint32_t rid) const
{
    auto it = neighbors.find(rid);
    if (it != neighbors.end())
        return &it->second;
    return nullptr;
}

std::optional<size_t> NeighborTable::addNeighborList(uint8_t* buf, size_t maxSize)
{
    size_t siz = neighbors.size();
    if (maxSize > siz * 4) return std::nullopt;
    size_t off = 0;
    for (auto& [rid, _] : neighbors)
    {
        utils::writeU32(buf + off, rid);
        off += 4;
    }
    return off;
}

void NeighborTable::cancelAllInactiveTimers()
{
    for (auto& [_, nbr] : neighbors)
        iface.getTimers().cancleInactiveTimer(nbr);
}
} // namespace routing
