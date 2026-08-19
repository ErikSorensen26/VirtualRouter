// OspfProcess.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include "configs/registry/global/GlobalRegistry.h"
#include <ControlScheduler.h>
#include <RCU.hpp>

#include "OspfProcess.h"
#include "area/Area.h"
#include "area/IntraOriginator.h"
#include "configs/FieldAccessor.hpp"

namespace routing::ospf
{
OspfProcess::OspfProcess(config::OspfRegistry& reg, bool isV3, uint16_t procId, types::AddressFamily af, core::VirtualRouter* vrf)
    : isV3(isV3),
      procId(procId),
      af(af),
      routingInstance(*vrf),
      rib(*this),
      scheduler(vrf->getControlScheduler().create()),
      interOriginator(*this),
      externalOriginator(*this),
      externalRouteManager(*this),
      ifaceMgr(*this),
      configs(reg),
      priv(*this)
{
    configs.context().set(this);
    calculateRID();

    // Subscribe to interface lifecycle events so the interface list stays
    auto& ifMgr = vrf->getInterfaceManager();

    if (!isV3)
    {
        auto postRefresh = [](void* ctx, interface::Interface&) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p]{ p->ifaceMgr.refreshInterfaceList(); });
        };
        priv.ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, postRefresh);
        priv.ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, postRefresh);

        auto postRefreshV4 = [](void* ctx, interface::Interface&, const types::IPPrefix&) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p]{ p->ifaceMgr.refreshInterfaceList(); });
        };
        priv.ipReadyId = ifMgr.subscribe(interface::IPEvent::IPV4_READY, this, postRefreshV4);
        priv.ipDelId   = ifMgr.subscribe(interface::IPEvent::IPV4_DEL,   this, postRefreshV4);
    }
    else
    {
        auto onIfUp = [](void* ctx, interface::Interface& iface) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p, &iface]{ p->ifaceMgr.addInterface(iface); });
        };
        auto onIfDown = [](void* ctx, interface::Interface& iface) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p, &iface]{ p->ifaceMgr.removeInterface(iface); });
        };
        priv.ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, onIfUp);
        priv.ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, onIfDown);

        auto onIpReadyV6 = [](void* ctx, interface::Interface& iface, const types::IPPrefix&) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p, &iface]{ p->ifaceMgr.addInterface(iface); });
        };
        auto onIpDelV6 = [](void* ctx, interface::Interface& iface, const types::IPPrefix&) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p, &iface]{ p->ifaceMgr.removeInterface(iface); });
        };
        priv.ipReadyId = ifMgr.subscribe(interface::IPEvent::IPV6_LL_READY, this, onIpReadyV6);
        priv.ipDelId   = ifMgr.subscribe(interface::IPEvent::IPV6_LL_DEL,   this, onIpDelV6);
    }
}

OspfProcess::Private::Private(OspfProcess& proc)
    : process(proc)
{}

OspfProcess::~OspfProcess()
{
    // Unsubscribe before the scheduler and interface state tear down.
    auto& ifMgr = routingInstance.getInterfaceManager();
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{priv.ifUpId});
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{priv.ifDownId});
    if (!isV3)
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPEventMgr::Id{priv.ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPEventMgr::Id{priv.ipDelId});
    }
    else
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPEventMgr::Id{priv.ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPEventMgr::Id{priv.ipDelId});
    }

    // Areas outlive this body but their timers touch state deactivateAll() tears down, so cancel them (Area::scheduler refs aren't reached by scheduler.release()) now.
    for (auto& [id, area] : priv.areas)
    {
        area.originContext.cancelAllTimers();
        area.scheduler.release();
    }

    scheduler.release();

    ifaceMgr.deactivateAll();
}

void OspfProcess::enqueueReset()
{
    scheduler.post([this] {
        for (auto& [id, area] : priv.areas)
            area.reset();
    });
}

void OspfProcess::beginGracefulRestart(uint32_t gracePeriodSeconds, GraceRestartReason reason)
{
    scheduler.post([this, gracePeriodSeconds, reason] {
        ifaceMgr.forEach([&](OspfInterfaceId, OspfInterfaceBase& iface) {
            iface.beginGracefulRestart(gracePeriodSeconds, reason);
        });
    });
}

void OspfProcess::endGracefulRestart()
{
    scheduler.post([this] {
        ifaceMgr.forEach([&](OspfInterfaceId, OspfInterfaceBase& iface) {
            iface.endGracefulRestart();
        });
    });
}

void OspfProcess::enqueueSyncNeighbor()
{
    scheduler.post([this] {
        ifaceMgr.syncNeighbors();
    });
}

void OspfProcess::enqueueSyncNetworks()
{
    scheduler.post([this] {
        ifaceMgr.refreshInterfaceList();
    });
}

void OspfProcess::enqueueSyncSummaries()
{
    scheduler.post([this] {
        externalOriginator.syncSummaryConfig();
    });
}

bool OspfProcess::calculateRID()
{
    uint32_t rid;
    bool calculated = routingInstance.calculateRID(rid);
    priv.rid.store(rid, std::memory_order_release);
    return calculated;
}

Area* OspfProcess::getArea(uint32_t areaId)
{
    auto it = priv.areas.find(areaId);
    if (it == priv.areas.end()) return nullptr;
    return &it->second;
}

OriginatorContext* OspfProcess::getOriginCtx(uint32_t areaId)
{
    if (Area* area = getArea(areaId); area)
        return &area->originContext;
    return nullptr;
}

Area& OspfProcess::insureArea(uint32_t areaId)
{
    if (!priv.areas.contains(areaId))
    {
        priv.areas.try_emplace(areaId, *this, areaId);
        setABR(priv.areas.size() > 1 && priv.areas.contains(0));
    }
    return priv.areas.at(areaId);
}

void OspfProcess::removeArea(uint32_t areaId)
{
    if (!priv.areas.contains(areaId)) return;
    priv.areas.erase(areaId);
    setABR(priv.areas.size() > 1 && priv.areas.contains(0));
}

size_t OspfProcess::areaSize() const
{
    return priv.areas.size();
}

void OspfProcess::setASBR(bool val)
{
    bool current = priv.asbr;
    if (current == val) return;

    priv.asbr = val;

    // Refresh default routes
    for (auto& [id, area] : priv.areas)
    {
        area.originator.fullRefresh();
    }
}

void OspfProcess::setABR(bool val)
{
    bool current = priv.abr;
    if (current == val) return;

    priv.abr = val;

    // Refresh ranges
    for (auto& [id, area] : priv.areas)
        area.syncRangeSuppression(area.getRanges(), true);
}

bool OspfProcess::isASBR() const
{
    return priv.asbr;
}

bool OspfProcess::isABR() const
{
    return priv.abr;
}

uint32_t OspfProcess::getRouterId() const
{
    const auto id = configs.get<config::Ospf::ROUTER_ID>();
    if (id.hasValue()) return id.load();
    return priv.rid.load(std::memory_order_relaxed);
}
} // namespace routing
