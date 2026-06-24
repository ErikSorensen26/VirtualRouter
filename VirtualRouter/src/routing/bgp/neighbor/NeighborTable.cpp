// OspfNeighborTable.cpp

#include <unordered_set>
#include <IPAddress.h>

#include "Neighbor.h"
#include "NeighborTable.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
NeighborTable::NeighborTable(BgpProcess& proc)
    : process(proc),
      peerTemplates(proc)
{}

void NeighborTable::syncNeighbors()
{
    syncPeerGroups();

    auto& configs = process.getConfigs();

    std::unordered_set<types::IPAddress> unseen;

    // Fill unseen with all current neighbors
    for (const auto& [addr, _] : neighbors)
        unseen.insert(addr);

    auto& neighborList = configs.get<config::Bgp::NEIGHBOR>().get();
    for (const auto& [ip, _] : neighborList)
    {
        if (unseen.contains(ip))
        {
            unseen.erase(ip);
        }
        else
        {
            createNeighbor(ip);
        }
    }

    for (const auto& nbr : unseen)
    {
        // Dynamic neighbors are not owned by the static config; skip them here.
        auto it = neighbors.find(nbr);
        if (it != neighbors.end() && it->second.dynamic)
            continue;
        deleteNeighbor(nbr);
    }
}

void NeighborTable::syncPeerGroups()
{
    peerTemplates.sync();
}

Neighbor* NeighborTable::createNeighbor(const types::IPAddress& ipAddress)
{
    if (neighbors.contains(ipAddress))
        return &neighbors.at(ipAddress);

    auto [it, ok] = neighbors.try_emplace(ipAddress, ipAddress, process);
    if (ok)
    {
        auto connectionMode = it->second.getConfigs().get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
        if (connectionMode.hasValue() && !connectionMode.load() /*active = true*/)
            process.startPassiveSession(it->second);
        process.startActiveSession(it->second);
    }
    return ok ? &it->second : nullptr;
}

Neighbor* NeighborTable::createDynamicNeighbor(const types::IPAddress& ipAddress, const std::string& peerGroupName)
{
    // Re-use an existing dynamic entry for the same address (reconnect case).
    if (auto it = neighbors.find(ipAddress); it != neighbors.end())
        return it->second.dynamic ? &it->second : nullptr;

    auto [it, ok] = neighbors.try_emplace(ipAddress, ipAddress, process);
    if (!ok)
        return nullptr;

    it->second.dynamic = true;

    // Attach the peer-group so REMOTE_AS, hold-time, etc. are inherited.
    if (PeerGroup* pg = lookupPeerGroup(peerGroupName))
        it->second.getConfigs().setPeerGroup(pg);

    // Do NOT start an active session — dynamic neighbors are inbound-only.
    return &it->second;
}

void NeighborTable::deleteNeighbor(const types::IPAddress& ipAddress)
{
    auto it = neighbors.find(ipAddress);
    if (it == neighbors.end())
        return;

    const uint32_t rid = it->second.rid;
    if (rid != 0)
        peers.erase(rid);

    neighbors.erase(it);
}

Neighbor* NeighborTable::lookup(const types::IPAddress& ipAddress)
{
    auto it = neighbors.find(ipAddress);
    return (it != neighbors.end()) ? &it->second : nullptr;
}

const Neighbor* NeighborTable::lookup(const types::IPAddress& ipAddress) const
{
    auto it = neighbors.find(ipAddress);
    return (it != neighbors.end()) ? &it->second : nullptr;
}

Neighbor* NeighborTable::lookup(uint32_t rid)
{
    auto it = peers.find(rid);
    return (it != peers.end()) ? it->second : nullptr;
}

const Neighbor* NeighborTable::lookup(uint32_t rid) const
{
    auto it = peers.find(rid);
    return (it != peers.end()) ? it->second : nullptr;
}

bool NeighborTable::activatePeer(const types::IPAddress& nbr, uint32_t rid)
{
    auto it = neighbors.find(nbr);
    if (it == neighbors.end())
        return false;

    peers[rid] = &it->second;
    it->second.rid = rid;
    return true;
}

bool NeighborTable::deactivatePeer(uint32_t rid)
{
    auto it = peers.find(rid);
    if (it == peers.end())
        return false;

    it->second->rid = 0;
    peers.erase(it);
    return true;
}

void NeighborTable::cancelAllHoldTimers()
{
    for (auto& [_, nbr] : neighbors)
    {
        if (nbr.session)
            nbr.session->getTimers().cancelAll();
    }
}

void NeighborTable::runDccCheck()
{
    for (auto& [_, nbr] : neighbors)
    {
        if (nbr.getConfigs().get<config::BgpNeighborSession::DISABLE_CONNECTION_CHECK>().load())
        {
            disableConnectionCheck = true;
            return;
        }
    }
    disableConnectionCheck = false;
}

PeerGroup& NeighborTable::createPeerGroup(const std::string& name)
{
    return peerTemplates.createPeerGroup(name);
}

void NeighborTable::removePeerGroup(const std::string& name)
{
    peerTemplates.removePeerGroup(name);
}

PeerSessionTemplate& NeighborTable::createPeerSessionTemplate(const std::string& name)
{
    return peerTemplates.createPeerSessionTemplate(name);
}

void NeighborTable::removePeerSessionTemplate(const std::string& name)
{
    peerTemplates.removePeerSessionTemplate(name);
}

PeerPolicyTemplate& NeighborTable::createPeerPolicyTemplate(const std::string& name)
{
    return peerTemplates.createPeerPolicyTemplate(name);
}

void NeighborTable::removePeerPolicyTemplate(const std::string& name)
{
    peerTemplates.removePeerPolicyTemplate(name);
}

PeerGroup* NeighborTable::lookupPeerGroup(const std::string& name)
{
    return peerTemplates.lookupPeerGroup(name);
}

const PeerGroup* NeighborTable::lookupPeerGroup(const std::string& name) const
{
    return peerTemplates.lookupPeerGroup(name);
}

PeerSessionTemplate* NeighborTable::lookupPeerSessionTemplate(const std::string& name)
{
    return peerTemplates.lookupPeerSessionTemplate(name);
}

const PeerSessionTemplate* NeighborTable::lookupPeerSessionTemplate(const std::string& name) const
{
    return peerTemplates.lookupPeerSessionTemplate(name);
}

PeerPolicyTemplate* NeighborTable::lookupPeerPolicyTemplate(const std::string& name)
{
    return peerTemplates.lookupPeerPolicyTemplate(name);
}

const PeerPolicyTemplate* NeighborTable::lookupPeerPolicyTemplate(const std::string& name) const
{
    return peerTemplates.lookupPeerPolicyTemplate(name);
}
} // namespace routing
