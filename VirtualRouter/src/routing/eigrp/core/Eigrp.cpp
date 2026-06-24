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
    selfRef(scheduler.ref()),
    routeManager(*this)
{
    // Subscribe to interface lifecycle events.
    auto& ifMgr = vrf->getInterfaceManager();

    // These callbacks fire synchronously from the underlying Interface (e.g.
    // Interface::cleanupInterface() -> notify(IF_DOWN, ...)) while that
    // Interface is still fully alive. They must use postAndWait(), not
    // post(), so that refreshInterfaceList() runs and prunes/rebuilds the
    // corresponding EigrpInterface *before* the calling thread proceeds to
    // destroy the Interface. A bare post() would let the Interface (and its
    // tx/buffer pool) be destroyed first, leaving the later-running
    // refreshInterfaceList() / ~EigrpInterface() holding a dangling
    // currentInterface pointer.
    auto postRefresh = [](void* ctx, interface::Interface&) {
        auto* e = static_cast<Eigrp*>(ctx);
        e->selfRef.postAndWait([e]{ e->refreshInterfaceList(); });
    };

    ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, postRefresh);
    ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, postRefresh);

    if (addressFamily == types::AddressFamily::IPv4)
    {
        auto postRefreshV4 = [](void* ctx, interface::Interface&, types::IPv4Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->selfRef.postAndWait([e]{ e->refreshInterfaceList(); });
        };
        ipReadyId = ifMgr.subscribe(interface::IPv4Event::IPV4_READY, this, postRefreshV4);
        ipDelId   = ifMgr.subscribe(interface::IPv4Event::IPV4_DEL,   this, postRefreshV4);
    }
    else
    {
        auto postRefreshV6 = [](void* ctx, interface::Interface&, types::IPv6Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->selfRef.postAndWait([e]{ e->refreshInterfaceList(); });
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

    // Release selfRef first: this blocks until any in-flight self-posted task
    // (e.g. refreshInterfaceList() from EigrpSyncNetworks/postRefresh) finishes,
    // and rejects any further posts, before shutdownInternal() tears down ifaceMgr.
    // selfRef is now dead, so call shutdownInternal() directly instead of the
    // posting public shutdown() -- nothing else can be touching ifaceMgr here.
    selfRef.release();

    shutdownInternal();

    scheduler.reset();
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

void Eigrp::shutdownInternal()
{
    ifaceMgr.deactivateAll();
}

void Eigrp::shutdown()
{
    // Post the teardown through selfRef so it is serialized against any
    // pending/in-flight refreshInterfaceList() (posted by postRefresh on
    // interface events), then wait for it to complete. Without this, a
    // direct ifaceMgr.deactivateAll() here could run concurrently with a
    // posted refreshInterfaceList() iterating eigrpInterfaceList on a
    // worker thread, causing a use-after-free.
    selfRef.post([this]{ shutdownInternal(); });
    scheduler.waitIdle();
}

void Eigrp::restart()
{
    // restart() is only reached via EigrpShutdown, which already runs
    // inside a task posted on this->scheduler -- call shutdownInternal()
    // directly rather than re-posting onto the same queue.
    shutdownInternal();
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
