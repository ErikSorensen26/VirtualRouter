// NeighborTable.cpp

#include <IPAddress.h>

#include "Neighbor.h"
#include "NeighborTable.h"
#include "bgp/BgpScope.h"

namespace routing::bgp
{
NeighborTable::NeighborTable(BgpScope& scope, PeerTemplateTable& table)
    : scope(scope),
      peerTemplates(table)
{}

Neighbor* NeighborTable::createNeighbor(config::BgpNeighborSessionRegistry& cfgs, const types::IPAddress& ipAddress)
{
    if (neighbors.contains(ipAddress))
        return &neighbors.at(ipAddress);

    auto [it, ok] = neighbors.try_emplace(ipAddress, cfgs, ipAddress, *this, scope.scheduler);
    if (!ok)
        return nullptr;

    return &it->second;
}

void NeighborTable::startConfiguredSession(Neighbor& nbr)
{
    auto connectionMode = nbr.getConfigs().get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
    if (connectionMode.hasValue() && connectionMode.load() == config::bgp::BgpConnectionMode::PASSIVE)
        scope.startPassiveSession(nbr);
    else
        scope.startActiveSession(nbr);
}

Neighbor* NeighborTable::createDynamicNeighbor(const types::IPAddress& ipAddress, const std::string& peerGroupName)
{
    // Re-use an existing dynamic entry for the same address (reconnect case).
    if (auto it = neighbors.find(ipAddress); it != neighbors.end())
        return it->second.getDynamic() ? &it->second : nullptr;

    PeerGroup* pg = lookupPeerGroup(peerGroupName);
    if (!pg) return nullptr;

    auto [it, ok] = neighbors.try_emplace(ipAddress, *pg, ipAddress, *this, scope.scheduler);
    if (!ok) return nullptr;

    // Do NOT start an active session — dynamic neighbors are inbound-only.
    return &it->second;
}

void NeighborTable::purgeDynamicNeighbors(PeerGroup* group)
{
    for (auto it = neighbors.begin(); it != neighbors.end();)
    {
        if (const PeerGroup* dyn = it->second.getDynamic(); dyn && group == dyn)
            it = neighbors.erase(it);
        else
            ++it;
    }
}

void NeighborTable::deleteNeighbor(const types::IPAddress& ipAddress)
{
    auto it = neighbors.find(ipAddress);
    if (it == neighbors.end())
        return;

    // Dynamic neighbors are not owned by the static config; leave them alone.
    if (it->second.getDynamic())
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
    return nbr ? nbr->findAfNeighbor(afi) : nullptr;
}

NeighborAf* NeighborTable::lookup(uint32_t rid, const AfiSafi& afi)
{
    Neighbor* nbr = lookup(rid);
    return nbr ? nbr->findAfNeighbor(afi) : nullptr;
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
        return field.load() == config::bgp::BgpConnectionMode::ACTIVE;
    return std::nullopt;
}

void NeighborTable::shutdownNeighbor(Neighbor& neighbor)
{
    scope.shutdownNeighbor(neighbor);
}

void NeighborTable::unshutdownNeighbor(Neighbor& neighbor)
{
    scope.unshutdownNeighbor(neighbor);
}

void NeighborTable::clear()
{
    peers.clear();
    neighbors.clear();
}

void NeighborTable::restartNeighbor(Neighbor& neighbor)
{
    // A session-reset config change bounces an existing session; it must not create one
    // where none exists.
    if (!scope.findSession(neighbor.neighborAddress))
        return;
    scope.shutdownNeighbor(neighbor);
    scope.unshutdownNeighbor(neighbor);
}

AddressFamilyVariant* NeighborTable::findAddressFamily(const AfiSafi& afi)
{
    return scope.findAddressFamily(afi);
}

void NeighborTable::syncPeerGroup(Neighbor& nbr)
{
    peerTemplates.syncNeighborPeerGroup(nbr);
}

void NeighborTable::syncPeerGroup(NeighborAf& nbr)
{
    peerTemplates.syncNeighborPeerGroup(nbr);
}

void NeighborTable::syncPeerSessionTemplate(Neighbor& nbr)
{
    peerTemplates.syncNeighborPeerSessionTemplate(nbr);
}

void NeighborTable::syncPeerPolicyTemplate(NeighborAf& nbr)
{
    peerTemplates.syncNeighborPeerPolicyTemplate(nbr);
}

PeerGroup* NeighborTable::lookupPeerGroup(const std::string& name)
{
    return peerTemplates.lookupPeerGroup(name);
}

bool NeighborTable::isPeerConfed(uint32_t peerAs) const
{
    bool inConfed = false;
    scope.configs().get<config::Bgp::BGP_CONFEDERATION_PEERS>().readEach(
        [&](const uint32_t peer)
        {
            if (peer == peerAs) { inConfed = true; return true; }
            return false;
        }
    );
    return inConfed;
}

const config::BgpRegistry& NeighborTable::getConfigs() const
{
    return scope.configs();
}
} // namespace routing
