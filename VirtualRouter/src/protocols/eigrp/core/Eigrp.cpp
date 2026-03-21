// Eigrp.cpp

#include <AddressFamily.hpp>
#include <VirtualRouter.h>

#include "Eigrp.h"
#include "eigrp/topology/TopologyTable.h"
#include "eigrp/rtp/Neighbor.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"

namespace EIGRP
{
Eigrp::Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named)
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
    start();
}

Eigrp::~Eigrp()
{
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

bool Eigrp::isInNetworkRange(IPv4Address testIp)
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

NamedEigrp::NamedEigrp(uint32_t& as, AddressFamily af, const std::string& name, VirtualRouter* vrf, bool /*multicast*/)
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
    auto* iface = routingInstance->getInterface(interfaceId);
    if (iface)
        getIfaceMgr().createInterface(iface);
}

void Eigrp::addGlobalNeighbor(const IPAddress& neighborIp, Neighbor* neighbor)
{
    allNeighbors[neighborIp] = neighbor;
}

void Eigrp::delGlobalNeighbor(const IPAddress& neighborIp)
{
    allNeighbors.erase(neighborIp);
}
}

