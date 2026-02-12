// Eigrp.cpp

// Finish auth and hash stuff

#include <AddressFamily.hpp>
#include <VirtualRouter.h>

#include "Eigrp.h"
#include "eigrp/topology/TopologyTable.h"
#include "eigrp/rtp/Neighbor.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"

namespace Eigrp
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

    std::shared_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);
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
    //TODO
}

void Eigrp::restart()
{
    // Shutdown current state
    shutdown();
}

void Eigrp::runMaintenance()
{
    topology.pruneStaleRoutes();
}

bool Eigrp::calculateRID()
{
    return routingInstance->calculateRID(rid.id);
}

void Eigrp::addGlobalNeighbor(const IPAddress& neighborIp, Neighbor* neighbor)
{
    std::lock_guard<std::mutex> globalLock(neighborMutex);
    allNeighbors[neighborIp] = neighbor;
}

void Eigrp::delGlobalNeighbor(const IPAddress& neighborIp)
{
    std::lock_guard<std::mutex> globalLock(neighborMutex);
    allNeighbors.erase(neighborIp);
}
}

