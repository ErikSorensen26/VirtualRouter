// Eigrp.cpp

#include <AddressFamily.hpp>
#include <VirtualRouter.h>

#include "Eigrp.h"
#include "IPAddress.h"
#include "eigrp/topology/TopologyTable.h"
#include "eigrp/rtp/Neighbor.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"

namespace routing::eigrp
{
Eigrp::Eigrp(uint32_t as, types::AddressFamily af, core::VirtualRouter* vrf, bool named)
  : routingInstance(vrf),
    asNumber(as),
    addressFamily(af),
    topology(*this),
    ifaceMgr(*this),
    configMgr(*this),
    aggregator(*this),
    namedMode(named),
    scheduler(vrf->getControlScheduler().create()),
    routeManager(*this)
{
    // Subscribe to interface lifecycle events.
    auto& ifMgr = vrf->getInterfaceManager();

    auto postRefresh = [](void* ctx, interface::Interface&) {
        auto* e = static_cast<Eigrp*>(ctx);
        e->scheduler.post([e]{ e->refreshInterfaceList(); });
    };

    ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, postRefresh);
    ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, postRefresh);

    if (addressFamily == types::AddressFamily::IPv4)
    {
        auto postRefreshV4 = [](void* ctx, interface::Interface&, types::IPv4Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->scheduler.post([e]{ e->refreshInterfaceList(); });
        };
        ipReadyId = ifMgr.subscribe(interface::IPv4Event::IPV4_READY, this, postRefreshV4);
        ipDelId   = ifMgr.subscribe(interface::IPv4Event::IPV4_DEL,   this, postRefreshV4);
    }
    else
    {
        auto postRefreshV6 = [](void* ctx, interface::Interface&, types::IPv6Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->scheduler.post([e]{ e->refreshInterfaceList(); });
        };
        ipReadyId = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_READY, this, postRefreshV6);
        ipDelId   = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_DEL,   this, postRefreshV6);
    }

    start();
}

Eigrp::~Eigrp()
{
    // Unsubscribe before shutdown so no further refreshes can be posted
    auto& ifMgr = routingInstance->getInterfaceManager();
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{ifUpId});
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{ifDownId});
    if (addressFamily == types::AddressFamily::IPv4)
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{ipDelId});
    }
    else
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{ipDelId});
    }
    shutdown();
}

void Eigrp::refreshInterfaceList()
{
    ifaceMgr.refreshInterfaceList();
}

void Eigrp::broadcastRouteChanges(const std::vector<const RouteInfo*>& changedRoutes)
{
    if (changedRoutes.empty())
        return;

    for (auto& [_, iface] : ifaceMgr.eigrpInterfaceList)
    {
        iface.notifyRoutingChange(changedRoutes);
    }
}

void Eigrp::start()
{

    calculateRID();

    refreshInterfaceList();
}

void Eigrp::shutdown()
{
    ifaceMgr.deactivateAll();
}

void Eigrp::restart()
{
    shutdown();
    start();
}

void Eigrp::runMaintenance()
{
    topology.pruneStaleRoutes();
}

bool Eigrp::calculateRID()
{
    return routingInstance->calculateRID(rid.id);
}

bool Eigrp::isInNetworkRange(types::IPv4Address testIp)
{
    return configMgr.isInNetworkRange(testIp);
}

void ClassicEigrp::initializeEigrp()
{
    start();
}

void ClassicEigrp::shutdown()
{
    Eigrp::shutdown();
}

NamedEigrp::NamedEigrp(uint32_t& as, types::AddressFamily af, const std::string& name, core::VirtualRouter* vrf, bool /*multicast*/)
    : Eigrp(as, af, vrf, true), processName(name)
{
}

void NamedEigrp::initializeEigrp()
{
    start();
}

void NamedEigrp::shutdown()
{
    Eigrp::shutdown();
}

void NamedEigrp::configureInterface(uint32_t interfaceId)
{
    auto* iface = routingInstance->getInterfaceManager().get(interfaceId);
    if (iface)
        getIfaceMgr().createInterface(iface);
}

void Eigrp::addGlobalNeighbor(const types::IPAddress& neighborIp, Neighbor* neighbor)
{
    allNeighbors[neighborIp] = neighbor;
}

void Eigrp::delGlobalNeighbor(const types::IPAddress& neighborIp)
{
    allNeighbors.erase(neighborIp);
}
} // namespace routing
