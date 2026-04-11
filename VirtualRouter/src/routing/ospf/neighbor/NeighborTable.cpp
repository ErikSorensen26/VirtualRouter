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

        // Update configs of all unicast neighbors (OspfInterface::NEIGHBOR has IgnoreCompare wrappers)
        ifaceConfigs.get<config::OspfInterface::NEIGHBOR>().withRead([&](const auto& nbrs)
        {
            for (const auto& entry : nbrs)
            {
                const auto& ip    = std::get<0>(entry);
                const auto& cost  = std::get<1>(entry).value;
                const auto& dbf   = std::get<2>(entry).value;
                const auto& poll  = std::get<3>(entry).value;
                const auto& prio  = std::get<4>(entry).value;
                if (!iface.interfaceAddress.contains(ip))
                    continue;
                unicastNbrs.erase(ip);
                unicast.try_emplace(
                    ip,
                    cost,
                    dbf,
                    poll.value_or(120),
                    prio.value_or(0)
                );
            }
        });

        // Ospf::NEIGHBORS uses double-nesting (ListField<vector<tuple<...>>>)
        iface.getArea().process().getConfigs().get<config::Ospf::NEIGHBORS>().withRead([&](const auto& nbrsList)
        {
            for (const auto& nbrs : nbrsList)
                for (const auto& [ip, cost, dbfilter, pollIntv, priority] : nbrs)
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
    auto it = neighbors.find(rid);
    if (it == neighbors.end()) return;

    // Formally tear down adjacency: flushes LSAs and clears retransmission lists
    it->second.setState(Neighbor::State::DOWN);
    neighbors.erase(it);
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
    if (maxSize < siz * 4) return std::nullopt; // buffer too small
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
