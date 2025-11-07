// EigrpInterfaceNeighborManager.cpp

#include "NeighborTable.h"
#include <EigrpTypes.hpp>
#include "ReliableTransport.h"
#include <EigrpInterface.h>
#include <EigrpCore.h>

namespace Eigrp
{
NeighborTable::NeighborTable(EigrpInterface& iface) : iface(iface) {}

NeighborTable::~NeighborTable()
{
    std::unique_lock<std::shared_mutex> intLock(neighborMutex);
    for (auto& [ip, neighbor] : neighbors)
    {
        deleteNeighbor(ip);
    }
}

Neighbor* NeighborTable::createNeighbor(const IPAddress& neighborIp, Neighbor::Version v, const uint8_t* macAddress)
{
    // Add neighbor only if it doesn't already exist
    std::unique_lock<std::shared_mutex> intLock(neighborMutex);

    bool unicast = macAddress == nullptr;

    auto& base = iface.getBase();

    auto it = neighbors.find(neighborIp);
    bool exists = it != neighbors.end();
    bool modeSwap = (unicast && exists && !it->second->unicast);
    if (!exists || modeSwap)
    {
        if (modeSwap)
        {
            base.delGlobalNeighbor(neighborIp);
            delete it->second;
            it->second = nullptr;
        }

        auto* neighbor = new Neighbor(
            iface, iface.getTimers(), neighborIp, v, unicast
        );

        if (!unicast) std::memcpy(neighbor->macAddress, macAddress, 6);
        neighbors[neighborIp] = neighbor;
        base.addGlobalNeighbor(neighborIp, neighbor);

        if (unicast && iface.configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            disableMulticast();
        }

        return neighbor;
    }

    return nullptr;
}

void NeighborTable::deleteNeighbor(const IPAddress& neighborIp, bool unicast)
{
    // Find the neighbor and remove it if present
    Neighbor* neighbor = nullptr;
    {
        std::unique_lock<std::shared_mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end() && neighborIt->second->unicast)
        {
            neighbor = neighborIt->second;
        }
    }

    // Remove the neighbor if found and is in unciast
    if (neighbor && neighbor->unicast == unicast)
    {
        onDown(*neighbor);
        // Cancel timers
        iface.getTimers().cancelNeighborTimers(*neighbor);
        iface.getBase().delGlobalNeighbor(neighborIp);
        iface.getTopController().onNeighborDown(neighborIp);
        
        {
            std::unique_lock<std::shared_mutex> lock(neighborMutex);
            neighbors.erase(neighborIp);
            delete &neighbor;
        }
    }
    else return;
}

std::vector<Neighbor*> NeighborTable::lookupUnicast()
{
    std::vector<Neighbor*> unicast;
    std::shared_lock<std::shared_mutex> lock(neighborMutex);
    for (auto& [_, neighbor] : neighbors)
    {
        if (neighbor->unicast)
            unicast.push_back(neighbor);
    }
    return unicast;
}

size_t NeighborTable::size()
{
    std::shared_lock<std::shared_mutex> lock(neighborMutex);
    return neighbors.size();
}

Neighbor* NeighborTable::lookup(const IPAddress& neighborIp)
{
    std::shared_lock<std::shared_mutex> lock(neighborMutex);
    auto it = neighbors.find(neighborIp);
    if (it != neighbors.end())
    {
        return it->second;
    }
    return nullptr;
}

void NeighborTable::cancelAllHoldTimers()
{
    auto& timeMgr = iface.getTimers();
    std::shared_lock<std::shared_mutex> lock(neighborMutex);
    for (auto& [_, neighbor] : neighbors)
    {
        timeMgr.cancelHoldTimer(*neighbor);
    }
}

void NeighborTable::onDown(Neighbor& neighbor)
{
    // Cancel timers
    iface.getTimers().cancelNeighborTimers(neighbor);
    iface.getBase().delGlobalNeighbor(neighbor.ipAddress);
    iface.getTopController().onNeighborDown(neighbor.ipAddress);
    
    {
        std::unique_lock<std::shared_mutex> lock(neighborMutex);
        neighbors.erase(neighbor.ipAddress);
        delete &neighbor;
        if (neighbors.empty())
        {
            enableMulticast();
        }
    }
}

void NeighborTable::startGracefulRestart(Neighbor& neighbor)
{
    iface.getTimers().startGracefulTimer(neighbor);
}

bool NeighborTable::validatePTP(const IPAddress& neighborIp)
{
    if (iface.configs->interfaceMode.load(std::memory_order_relaxed) == EigrpConfigs::Mode::POINT_TO_POINT)
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
