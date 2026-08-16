// Eigrp.cpp

#include <AddressFamily.hpp>
#include <VirtualRouter.h>
#include <EnumBitMap.hpp>

#include "Eigrp.h"
#include "IPAddress.h"
#include "eigrp/topology/TopologyTable.h"
#include "eigrp/rtp/Neighbor.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "configs/FieldAccessor.hpp"

namespace routing::eigrp
{
Eigrp::Eigrp(config::EigrpRegistry& reg, uint16_t as, types::AddressFamily af, core::VirtualRouter* vrf)
    : asNumber(as),
      addressFamily(af),
      scheduler(vrf->getControlScheduler().create()),
      configs(reg),
      routingInstance(vrf),
      priv(*this),
      routeManager(*this)
{
    configs.context().set(this);
    // Subscribe to interface lifecycle events. Each event targets exactly
    // the interface that fired it -- no bulk sweep over all interfaces.
    auto& ifMgr = vrf->getInterfaceManager();

    auto onIfUp = [](void* ctx, interface::Interface& iface) {
        auto* e = static_cast<Eigrp*>(ctx);
        e->scheduler.postAndWait([e, &iface]{ e->priv.ifaceMgr.tryCreateInterface(iface); });
    };
    auto onIfDown = [](void* ctx, interface::Interface& iface) {
        auto* e = static_cast<Eigrp*>(ctx);
        interface::InterfaceKey key = iface.configs.key;
        e->scheduler.postAndWait([e, key]{ e->priv.ifaceMgr.destroyInterface(key); });
    };

    priv.ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, onIfUp);
    priv.ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, onIfDown);

    if (addressFamily == types::AddressFamily::IPv4)
    {
        auto onIpReadyV4 = [](void* ctx, interface::Interface& iface, types::IPv4Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->scheduler.postAndWait([e, &iface]{ e->priv.ifaceMgr.tryCreateInterface(iface); });
        };
        auto onIpDelV4 = [](void* ctx, interface::Interface& iface, types::IPv4Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            interface::InterfaceKey key = iface.configs.key;
            e->scheduler.postAndWait([e, key]{ e->priv.ifaceMgr.destroyInterface(key); });
        };
        priv.ipReadyId = ifMgr.subscribe(interface::IPv4Event::IPV4_READY, this, onIpReadyV4);
        priv.ipDelId   = ifMgr.subscribe(interface::IPv4Event::IPV4_DEL,   this, onIpDelV4);
    }
    else
    {
        auto onIpReadyV6 = [](void* ctx, interface::Interface& iface, types::IPv6Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->scheduler.postAndWait([e, &iface]{ e->priv.ifaceMgr.tryCreateInterface(iface); });
        };
        auto onIpDelV6 = [](void* ctx, interface::Interface& iface, types::IPv6Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            interface::InterfaceKey key = iface.configs.key;
            e->scheduler.postAndWait([e, key]{ e->priv.ifaceMgr.destroyInterface(key); });
        };
        priv.ipReadyId = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_READY, this, onIpReadyV6);
        priv.ipDelId   = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_DEL,   this, onIpDelV6);
    }

    start();
}

Eigrp::Private::Private(Eigrp& eigrp)
    : topology(eigrp),
      ifaceMgr(eigrp),
      aggregator(eigrp)
{}

Eigrp::~Eigrp()
{
    // Unsubscribe before shutdown so no further refreshes can be posted
    auto& ifMgr = routingInstance->getInterfaceManager();
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{priv.ifUpId});
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{priv.ifDownId});
    if (addressFamily == types::AddressFamily::IPv4)
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{priv.ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{priv.ipDelId});
    }
    else
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{priv.ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{priv.ipDelId});
    }

    scheduler.release();

    priv.shutdown();

    scheduler.reset();
}

void Eigrp::enqueueSetAutoSummarization(bool enable)
{
    scheduler.post([this, enable] {
        priv.aggregator.enableAutoSummary(enable);
    });
}

void Eigrp::enqueueSetShutdown(bool shut)
{
    scheduler.post([this, shut] {
        if (shut)
            priv.shutdown();
        else
            start();
    });
}

void Eigrp::enqueueSyncTopology()
{
    scheduler.post([this] {
        priv.topology.recalculateAll();
    });
}

void Eigrp::enqueueSetUnicastNeighbor(const types::IPAddress& addr, interface::InterfaceKey* key)
{
    scheduler.post([this, key, addr] {
        if (key)
        {
            getNeighborContext(addr).key = *key;
            EigrpInterface* iface = priv.ifaceMgr.getInterface(*key);
            if (iface) iface->ntable.createNeighbor(addr, Neighbor::Version::UNKNOWN, true);
        }
        else
        {
            EigrpInterface* iface = priv.ifaceMgr.getInterface(getNeighborContext(addr).key);
            if (iface) iface->ntable.deleteNeighbor(addr, true);
        }
    });
}

void Eigrp::enqueueSetPassive(const interface::InterfaceKey& key, bool passive)
{
    scheduler.post([this, key, passive] {
        for (auto& [ifaceKey, iface] : priv.ifaceMgr.eigrpInterfaceList)
            if (ifaceKey == key)
            {
                if (!iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().overridden())
                    iface.setPassive(passive);
                return;
            }
    });
}

void Eigrp::enqueueSetRouterId(std::optional<uint32_t> rid)
{
    scheduler.post([this, rid] {
        if (rid.has_value())
            setRouterID(rid.value());
        else
            clearRouterID();
    });
}

void Eigrp::enqueueSetAfInterface(const interface::InterfaceKey& key, config::EigrpInterfaceRegistry* reg)
{
    scheduler.post([this, key, reg] {
        if (reg)
            priv.ifaceMgr.createInterface(key, *reg);
        else
            priv.ifaceMgr.destroyInterface(key);
    });
}

EigrpNeighborContext& Eigrp::getNeighborContext(const types::IPAddress& addr)
{
    if (auto it = nbrContext.find(addr); it != nbrContext.end())
        return it->second;
    auto [it, _] = nbrContext.try_emplace(addr, *this, addr);
    return it->second;
}

void Eigrp::eraseNeighborContext(const types::IPAddress& addr)
{
    nbrContext.erase(addr);
}

void Eigrp::broadcastRouteChanges(const std::vector<const RouteInfo*>& changedRoutes)
{
    if (changedRoutes.empty())
        return;

    for (auto& [_, iface] : priv.ifaceMgr.eigrpInterfaceList)
    {
        iface.notifyRoutingChange(changedRoutes);
    }
}

void Eigrp::start()
{
    calculateRID();

    for (auto& [key, iface] : routingInstance->getInterfaceManager().snapshot())
    {
        if (iface && !iface->shutdownFlag.load(std::memory_order_relaxed))
            priv.ifaceMgr.tryCreateInterface(*iface);
    }
}

void Eigrp::Private::shutdown()
{
    ifaceMgr.deactivateAll();
}

void Eigrp::shutdown()
{
    scheduler.post([this]{ priv.shutdown(); });
    scheduler.waitIdle();
}

void Eigrp::restart()
{
    priv.shutdown();
    start();
}

void Eigrp::runMaintenance()
{
    priv.topology.pruneStaleRoutes();
}

bool Eigrp::calculateRID()
{
    return routingInstance->calculateRID(priv.rid.id);
}

bool Eigrp::isPassive(interface::InterfaceKey key) const
{
    return configs.get<config::Eigrp::PASSIVE_INTERFACES>().contains(key);
}

std::unordered_set<types::IPAddress> Eigrp::getUnicastNeighbors(interface::InterfaceKey key) const
{
    std::unordered_set<types::IPAddress> result;
    auto nbrs = configs.get<config::Eigrp::NEIGHBOR>();
    for (const auto& [addr, nbr] : nbrs)
    {
        auto ifaceKey = nbr->get<config::EigrpNeighbor::INTERFACE>();
        if (ifaceKey.hasValue() && ifaceKey.load() == key)
            result.insert(addr);
    }
    return result;
}

void Eigrp::enableStub(bool isStub, bool advertiseConnected, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
{
    if (!isStub) {
        configs.get<config::Eigrp::STUB>().unset();
        return;
    }
    types::EnumBitMap<config::eigrp::Stub> bm;
    bm.reset();
    if (advertiseConnected)     bm.set(config::eigrp::Stub::CONNECTED);
    if (advertiseStatic)        bm.set(config::eigrp::Stub::STATIC);
    if (advertiseSummary)       bm.set(config::eigrp::Stub::SUMMARY);
    if (advertiseRedistributed) bm.set(config::eigrp::Stub::REDISTRIBUTED);
    configs.get<config::Eigrp::STUB>().set(bm.raw());
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
