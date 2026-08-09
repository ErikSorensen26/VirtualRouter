// OspfNeighborTable.cpp

#include "Neighbor.h"
#include "NeighborTable.h"
#include "ospf/interface/OspfInterfaceBase.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/transmission/PacketDispatcher.h"
#include "ospf/OspfProcess.h"

namespace routing::ospf
{
NeighborTable::NeighborTable(OspfInterfaceBase& iface, InterfaceTimers& t)
    : iface(iface), tmgr(t) {}

void NeighborTable::syncUnicast()
{
    if (iface.isVirtualLink())
        return;

    auto& concreteIface = static_cast<OspfInterface&>(iface);
    auto ntype = iface.getNetworkType();

    if (ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT || ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        std::unordered_set<types::IPAddress> unicastNbrs;

        // Snap shot of all current neighbors
        for (const auto& [ip, _] : unicast)
            unicastNbrs.insert(ip);

        // Update configs of all unicast neighbors (OspfInterface::NEIGHBOR has IgnoreCompare wrappers)
        concreteIface.configs.get<config::OspfInterface::NEIGHBOR>().withRead([&](const auto& nbrs)
        {
            for (const auto& entry : nbrs)
            {
                const auto& ip    = std::get<0>(entry);
                const auto& cost  = std::get<1>(entry).value;
                const auto& dbf   = std::get<2>(entry).value;
                const auto& poll  = std::get<3>(entry).value;
                const auto& prio  = std::get<4>(entry).value;
                if (!concreteIface.interfaceAddress.contains(ip))
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

        iface.getProcessConfigs().get<config::Ospf::NEIGHBORS>().withRead([&](const auto& nbrsList)
        {
            for (const auto& nbrs : nbrsList)
            {
                const auto& [ip, cost, dbfilter, pollIntv, priority] = nbrs;
                if (!concreteIface.interfaceAddress.contains(ip))
                    continue;
                unicastNbrs.erase(ip);
                unicast.try_emplace(
                    ip,
                    cost.value,
                    dbfilter.value,
                    pollIntv.value.value_or(120),
                    priority.value.value_or(0)
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

void NeighborTable::resetNeighbors()
{
    for (auto it = neighbors.begin(); it != neighbors.end();)
    {
        tmgr.cancleInactiveTimer(it->second);
        auto next = std::next(it);
        Neighbor& nbr = it->second;
        nbr.setState(Neighbor::State::DOWN);
        it = next;
    }
}

Neighbor* NeighborTable::createNeighbor(uint32_t rid, const types::IPAddress& ipAddress, bool isUnicast)
{
    auto it = neighbors.find(rid);
    if (it != neighbors.end())
        return &it->second;

    // Neighbor constructor takes types::IPAddress& (non-const), so make a mutable copy
    types::IPAddress ip = ipAddress;
    auto [ins, ok] = neighbors.try_emplace(rid, iface, tmgr, rid, ip, isUnicast);
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

void NeighborTable::cancelAllInactiveTimers()
{
    for (auto& [rid, nbr] : neighbors)
    {
        if (nbr.getState() != Neighbor::State::FULL)
            tmgr.cancleInactiveTimer(nbr);
    }
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
        utils::write<uint32_t>(buf + off, rid);
        off += 4;
    }
    return off;
}

std::vector<uint32_t> NeighborTable::getNeighborRIDs() const
{
    std::vector<uint32_t> rids;
    rids.reserve(neighbors.size() + 1);
    rids.push_back(iface.process.getRouterId());
    for (const auto& [rid, _] : neighbors)
        rids.push_back(rid);
    return rids;
}
} // namespace routing
