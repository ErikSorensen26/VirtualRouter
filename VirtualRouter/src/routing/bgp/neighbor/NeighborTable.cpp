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

    std::unordered_set<types::IPAddress> unseen;

    // Fill unseen with all current neighbors
    for (const auto& [addr, _] : neighbors)
        unseen.insert(addr);

    auto& neighborList = process.configs.get<config::Bgp::NEIGHBOR>().get();
    for (const auto& [ip, _] : neighborList)
    {
        if (unseen.contains(ip))
        {
            unseen.erase(ip);
        }
        else
        {
            Neighbor* nbr = createNeighbor(ip);
            if (nbr)
                startConfiguredSession(*nbr);
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

    auto [it, ok] = neighbors.try_emplace(ipAddress, ipAddress, *this, process.scheduler);
    return ok ? &it->second : nullptr;
}

void NeighborTable::startConfiguredSession(Neighbor& nbr)
{
    auto connectionMode = nbr.configs.get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
    if (connectionMode.hasValue() && !connectionMode.load() /*active = true*/)
        process.startPassiveSession(nbr);
    else
        process.startActiveSession(nbr);
}

Neighbor* NeighborTable::createDynamicNeighbor(const types::IPAddress& ipAddress, const std::string& peerGroupName)
{
    // Re-use an existing dynamic entry for the same address (reconnect case).
    if (auto it = neighbors.find(ipAddress); it != neighbors.end())
        return it->second.dynamic ? &it->second : nullptr;

    auto [it, ok] = neighbors.try_emplace(ipAddress, ipAddress, *this, process.scheduler);
    if (!ok)
        return nullptr;

    it->second.dynamic = true;

    // Attach the peer-group so REMOTE_AS, hold-time, etc. are inherited.
    if (PeerGroup* pg = lookupPeerGroup(peerGroupName))
        it->second.configs.setPeerGroup(pg);

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

Neighbor* NeighborTable::lookup(uint32_t rid)
{
    auto it = peers.find(rid);
    return (it != peers.end()) ? it->second : nullptr;
}

NeighborAf* NeighborTable::lookup(const types::IPAddress& ipAddress, const AfiSafi& afi)
{
    Neighbor* nbr = lookup(ipAddress);
    return nbr
        ? &nbr->getAfNeighbor(afi)
        : nullptr;
}

NeighborAf* NeighborTable::lookup(uint32_t rid, const AfiSafi& afi)
{
    Neighbor* nbr = lookup(rid);
    return nbr
        ? &nbr->getAfNeighbor(afi)
        : nullptr;
}

bool NeighborTable::activatePeer(uint32_t rid, Session& sess)
{
    peers[rid] = &sess.activatePeer(rid);
    return true;
}

bool NeighborTable::deactivatePeer(Session& sess)
{
    auto it = peers.find(sess.getPeerRid());
    if (it == peers.end())
        return false;

    peers.erase(it);
    sess.deactivatePeer();
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
        if (nbr.configs.get<config::BgpNeighborSession::DISABLE_CONNECTION_CHECK>().load())
        {
            disableConnectionCheck = true;
            return;
        }
    }
    disableConnectionCheck = false;
}

bool NeighborTable::isShutdown(const Neighbor& nbr) const
{
    return nbr.configs.get<config::BgpNeighborSession::SHUTDOWN>().load();
}

bool NeighborTable::isConnectionCheck(const Neighbor& nbr) const
{
    return nbr.isEbgp() &&
        !nbr.configs.get<config::BgpNeighborSession::DISABLE_CONNECTION_CHECK>().load() &&
        !nbr.configs.get<config::BgpNeighborSession::EBGP_MULTIHOP>().load();
}

std::optional<bool> NeighborTable::isTcpConnectionMode(const Neighbor& nbr) const
{
    auto field = nbr.configs.get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
    if (field.hasValue())
        return field.load();
    return std::nullopt;
}

void NeighborTable::shutdownNeighbor(Neighbor& neighbor)
{
    process.shutdownNeighbor(neighbor);
}

void NeighborTable::unshutdownNeighbor(Neighbor& neighbor)
{
    process.unshutdownNeighbor(neighbor);
}

AddressFamilyVariant* NeighborTable::findAddressFamily(const AfiSafi& afi)
{
    return process.findAddressFamily(afi);
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

config::BgpNeighborSessionRegistry& NeighborTable::ensureNeighborConfigs(types::IPAddress addr)
{
    return process.configs.get<config::Bgp::NEIGHBOR>().emplaceBack(addr);
}

void NeighborTable::removeNeighborConfigs(types::IPAddress addr)
{
    process.configs.get<config::Bgp::NEIGHBOR>().erase(addr);
}

bool NeighborTable::isPeerConfed(uint32_t peerAs) const
{
    bool inConfed = false;
    process.configs.get<config::Bgp::BGP_CONFEDERATION_PEERS>().withRead(
        [&](const auto& peersList) {
            for (uint32_t p : peersList)
                    if (p == peerAs) { inConfed = true; return; }
        });
    return inConfed;
}
} // namespace routing
