// Eigrp.cpp

#include <algorithm>
#include <AddressFamily.hpp>
#include <VirtualRouter.h>
#include <EnumBitMap.hpp>

#include "Eigrp.h"
#include "IPAddress.h"
#include "eigrp/topology/TopologyTable.h"
#include "eigrp/rtp/Neighbor.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "configs/registry/global/VrfRegistry.h"
#include "configs/FieldAccessor.hpp"

namespace routing::eigrp
{
static config::EigrpRegistry& resolveEigrpRegistry(core::VirtualRouter* vrf, types::AddressFamily af, uint32_t as)
{
    if (af == types::AddressFamily::IPv4)
        return vrf->getConfigs().get<config::Vrf::ROUTER_EIGRP_V4>().emplaceBack(static_cast<uint16_t>(as));
    return vrf->getConfigs().get<config::Vrf::ROUTER_EIGRP_V6>().emplaceBack(static_cast<uint16_t>(as));
}

Eigrp::Eigrp(uint32_t as, types::AddressFamily af, core::VirtualRouter* vrf, bool named)
    : routingInstance(vrf),
      asNumber(as),
      addressFamily(af),
      namedMode(named),
      scheduler(vrf->getControlScheduler().create()),
      configs(resolveEigrpRegistry(vrf, af, as)),
      priv(*this),
      routeManager(*this)
{
    configs.context().set(this);
    // Subscribe to interface lifecycle events.
    auto& ifMgr = vrf->getInterfaceManager();

    auto postRefresh = [](void* ctx, interface::Interface&) {
        auto* e = static_cast<Eigrp*>(ctx);
        e->scheduler.postAndWait([e]{ e->refreshInterfaceList(); });
    };

    priv.ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, postRefresh);
    priv.ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, postRefresh);

    if (addressFamily == types::AddressFamily::IPv4)
    {
        auto postRefreshV4 = [](void* ctx, interface::Interface&, types::IPv4Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->scheduler.postAndWait([e]{ e->refreshInterfaceList(); });
        };
        priv.ipReadyId = ifMgr.subscribe(interface::IPv4Event::IPV4_READY, this, postRefreshV4);
        priv.ipDelId   = ifMgr.subscribe(interface::IPv4Event::IPV4_DEL,   this, postRefreshV4);
    }
    else
    {
        auto postRefreshV6 = [](void* ctx, interface::Interface&, types::IPv6Prefix&) {
            auto* e = static_cast<Eigrp*>(ctx);
            e->scheduler.postAndWait([e]{ e->refreshInterfaceList(); });
        };
        priv.ipReadyId = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_READY, this, postRefreshV6);
        priv.ipDelId   = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_DEL,   this, postRefreshV6);
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

void Eigrp::enqueueRefreshInterfaceList()
{
    scheduler.post([this] {
        refreshInterfaceList();
    });
}

void Eigrp::enqueueShutdown()
{
    scheduler.post([this] {
        bool isShutdown = configs.get<config::Eigrp::SHUTDOWN>().load();
        if (isShutdown)
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

void Eigrp::enqueueSyncPassive()
{
    scheduler.post([this] {
        for (auto& [key, iface] : priv.ifaceMgr.eigrpInterfaceList)
            iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().set(isPassive(key));
    });
}

void Eigrp::enqueueSyncRouterId()
{
    scheduler.post([this] {
        auto ridField = configs.get<config::Eigrp::ROUTER_ID>();
        if (ridField.hasValue())
            setRouterID(ridField.load());
        else
            clearRouterID();
    });
}

void Eigrp::refreshInterfaceList()
{
    priv.ifaceMgr.refreshInterfaceList();
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

    refreshInterfaceList();
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

bool Eigrp::isInNetworkRange(types::IPv4Address testIp) const
{
    bool found = false;
    configs.get<config::Eigrp::NETWORK>().withRead([&](const auto& v) {
        for (const auto& [ipAddr, prefLenW] : v) {
            if (!ipAddr.isIPv4()) continue;
            uint32_t addr = ipAddr.v4();
            uint8_t prefLen = prefLenW.value;
            uint32_t mask = (prefLen == 0) ? 0u : (~0u << (32 - prefLen));
            if ((testIp.addr & mask) == (addr & mask)) {
                found = true;
                return;
            }
        }
    });
    return found;
}

bool Eigrp::isPassive(interface::InterfaceKey key) const
{
    bool found = false;
    configs.get<config::Eigrp::PASSIVE_INTERFACES>().withRead([&](const std::vector<interface::InterfaceKey>& v) {
        found = std::find(v.begin(), v.end(), key) != v.end();
    });
    return found;
}

std::unordered_set<types::IPAddress> Eigrp::getUnicastNeighbors(interface::InterfaceKey key) const
{
    std::unordered_set<types::IPAddress> result;
    configs.get<config::Eigrp::NEIGHBOR>().withRead([&](const std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) {
        for (const auto& [ip, ifaceKey] : v)
            if (ifaceKey == key)
                result.insert(ip);
    });
    return result;
}

void Eigrp::addNetworkRange(const types::IPv4Prefix& newNetwork)
{
    if (addressFamily != types::AddressFamily::IPv4) return;

    types::IPAddress ip(newNetwork.addr);
    uint8_t prefLen = newNetwork.prefixLength;

    configs.get<config::Eigrp::NETWORK>().withWrite([&](auto& v) -> bool {
        for (const auto& [a, p] : v)
            if (a == ip && p.value == prefLen) return false;
        v.emplace_back(ip, config::IgnoreCompare<uint8_t>{prefLen});
        return true;
    });
}

void Eigrp::delNetworkRange(const types::IPv4Prefix& delNetwork)
{
    if (addressFamily != types::AddressFamily::IPv4) return;

    types::IPAddress ip(delNetwork.addr);
    uint8_t prefLen = delNetwork.prefixLength;

    bool removed = false;
    configs.get<config::Eigrp::NETWORK>().withWrite([&](auto& v) -> bool {
        auto it = std::find_if(v.begin(), v.end(), [&](const auto& t) {
            return std::get<0>(t) == ip && std::get<1>(t).value == prefLen;
        });
        if (it != v.end()) {
            v.erase(it);
            removed = true;
            return true;
        }
        return false;
    });

    if (removed)
        enqueueRefreshInterfaceList();
}

void Eigrp::clearNetworks()
{
    configs.get<config::Eigrp::NETWORK>().withWrite([](auto& v) -> bool {
        v.clear();
        return true;
    });
    enqueueRefreshInterfaceList();
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

void Eigrp::setPassiveInterface(interface::InterfaceKey key, bool add)
{
    configs.get<config::Eigrp::PASSIVE_INTERFACES>().withWrite([&](std::vector<interface::InterfaceKey>& v) -> bool {
        if (add) {
            if (std::find(v.begin(), v.end(), key) == v.end()) {
                v.push_back(key);
                return true;
            }
        } else {
            v.erase(std::remove(v.begin(), v.end(), key), v.end());
            return true;
        }
        return false;
    });

    auto* eigrpIface = priv.ifaceMgr.getInterface(key);
    if (eigrpIface)
        eigrpIface->configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().set(add);
}

void Eigrp::enableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key)
{
    configs.get<config::Eigrp::NEIGHBOR>().withWrite([&](std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) -> bool {
        for (const auto& [ip, k] : v)
            if (ip == neighborIp && k == key) return false;
        v.emplace_back(neighborIp, key);
        return true;
    });

    auto* iface = priv.ifaceMgr.getInterface(key);
    if (iface)
        iface->ntable.createNeighbor(neighborIp, Neighbor::Version::UNKNOWN, true);
}

void Eigrp::disableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key)
{
    configs.get<config::Eigrp::NEIGHBOR>().withWrite([&](std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) -> bool {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& t) {
            return std::get<0>(t) == neighborIp && std::get<1>(t) == key;
        }), v.end());
        return true;
    });

    auto* iface = priv.ifaceMgr.getInterface(key);
    if (iface)
        iface->ntable.deleteNeighbor(neighborIp, true);
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
