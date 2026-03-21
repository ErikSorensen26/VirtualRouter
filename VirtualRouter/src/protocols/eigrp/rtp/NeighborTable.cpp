// EigrpInterfaceNeighborManager.cpp

#include "NeighborTable.h"
#include "eigrp/EigrpTypes.hpp"
#include "ReliableTransport.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/core/Eigrp.h"

namespace Eigrp
{
NeighborTable::NeighborTable(EigrpInterface& iface) : iface(iface)
{
    auto& configs = iface.getBase().getConfigs();
    if (auto it = configs.unicastNeighbors.find(iface.interfaceKey); it != configs.unicastNeighbors.end())
        for (auto& ip : it->second)
            createNeighbor(ip, Neighbor::Version::UNKNOWN, true);
}

Neighbor* NeighborTable::createNeighbor(const IPAddress& neighborIp, Neighbor::Version v, bool isUnicast)
{
    // Add neighbor only if it doesn't already exist
    auto& base = iface.getBase();

    if (isUnicast)
    {
        base.getConfigs().unicastNeighbors[iface.interfaceKey].insert(neighborIp);
        unicast.insert(neighborIp);
    }

    auto it = neighbors.find(neighborIp);
    bool exists = it != neighbors.end();
    bool modeSwap = (exists && isUnicast != it->second.unicast);
    if (!exists || modeSwap)
    {
        if (modeSwap)
        {
            if (!isUnicast) unicast.erase(neighborIp);
            base.delGlobalNeighbor(neighborIp);
        }

        auto neighborIt = neighbors.try_emplace(neighborIp, iface, iface.getTimers(), neighborIp, v, isUnicast);
        if (!neighborIt.second) return nullptr;
        auto* neighbor = &neighborIt.first->second;
        base.addGlobalNeighbor(neighborIp, neighbor);

        if (isUnicast && iface.configs.multicastEnabled.load(std::memory_order_relaxed))
        {
            disableMulticast();
        }

        return neighbor;
    }

    return nullptr;
}

void NeighborTable::enableMulticast()
{
    if (!iface.configs.multicastEnabled.load(std::memory_order_relaxed))
    {
        iface.configs.multicastEnabled.store(true, std::memory_order_release);
    }
}

void NeighborTable::disableMulticast()
{
    // Check if multicast is already disabled
    if (!iface.configs.multicastEnabled.load(std::memory_order_relaxed)) return;

    iface.configs.multicastEnabled.store(false, std::memory_order_release);
    removeAllMulticast();
}

void NeighborTable::removeAllMulticast()
{
    for (auto it = neighbors.begin(); it != neighbors.end();)
    {
        if (!it->second.unicast)
        {
            onDown(it->second);
        }
        else
        {
            it++;
        }
    }
}

void NeighborTable::deleteNeighbor(const IPAddress& neighborIp, bool isUnicast)
{
    // Find the neighbor and remove it if present
    auto neighborIt = neighbors.find(neighborIp);
    if (neighborIt != neighbors.end())
    {
        if (isUnicast != neighborIt->second.unicast) return;
        neighbors.erase(neighborIt->second.ipAddress);
        if (isUnicast)
        {
            unicast.erase(neighborIp);
            if (unicast.empty() || neighbors.empty())
            {
                enableMulticast();
            }
        }
    }
}

std::vector<Neighbor*> NeighborTable::lookupUnicast()
{
    std::vector<Neighbor*> unicastNeighbors;
    for (auto& [_, neighbor] : neighbors)
    {
        if (neighbor.unicast)
            unicastNeighbors.push_back(&neighbor);
    }
    return unicastNeighbors;
}

size_t NeighborTable::size()
{
    return neighbors.size();
}

Neighbor* NeighborTable::lookup(const IPAddress& neighborIp)
{
    auto it = neighbors.find(neighborIp);
    if (it != neighbors.end())
    {
        return &it->second;
    }
    return nullptr;
}

void NeighborTable::cancelAllHoldTimers()
{
    auto& timeMgr = iface.getTimers();
    for (auto& [_, neighbor] : neighbors)
    {
        timeMgr.cancelHoldTimer(neighbor);
    }
}

void NeighborTable::onDown(Neighbor& neighbor)
{
    neighbors.erase(neighbor.ipAddress);
    if (neighbors.empty())
    {
        enableMulticast();
    }
}

void NeighborTable::resync()
{
    for (auto it = neighbors.begin(); it != neighbors.end();)
    {
        if ((static_cast<uint16_t>(it->second.tlvType) & 0xFF00) == 0x0600)
        {
            iface.getTopController().onNeighborDown(it->second);
            iface.getRtp().sendFullTopology(it->second, ReliableTransport::Resync::INIT);
            it++;
        }
        else
        {
            it = neighbors.erase(it);
        }
    }
}

void NeighborTable::startGracefulRestart(Neighbor& neighbor)
{
    iface.getTimers().startGracefulTimer(neighbor);
}

bool NeighborTable::validatePTP(const IPAddress& neighborIp)
{
    if (iface.configs.interfaceMode.load(std::memory_order_relaxed) == EigrpConfigs::Mode::POINT_TO_POINT)
    {
        // If no neighbors yet, allow.
        if (neighbors.empty())
            return true;

        // In point-to-point mode, ensure there's only one neighbor
        if (neighbors.size() == 1 && lookup(neighborIp))
            return true;

        return false;
    }
    return true; // Not P2P mode
}
}
