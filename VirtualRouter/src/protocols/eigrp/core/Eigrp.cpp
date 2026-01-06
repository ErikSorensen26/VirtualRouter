// Finish auth and hash stuff
#include <Eigrp.h>
#include <AddressFamily.hpp>
#include <TopologyTable.h>
#include <VirtualRouter.h>
#include <Interface.h>
#include <InterfaceType.hpp>
#include <Neighbor.h>
#include <EigrpInterface.h>

namespace Eigrp
{
Eigrp::Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named)
  : routingInstance(vrf),
    asNumber(as),
    addressFamily(af),
    configMgr(*this),
    ifaceMgr(*this),
    aggregator(*this),
    topology(*this),
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

