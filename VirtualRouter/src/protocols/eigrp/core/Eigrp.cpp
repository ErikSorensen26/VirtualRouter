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
    routeManager(*this),
    configMgr(*this),
    ifaceMgr(*this),
    aggregator(*this),
    topology(*this),
    namedMode(named),
    asNumber(as),
    addressFamily(af)
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

void Eigrp::calculateRID()
{
    uint32_t highestIP = 0;
    uint32_t tempIp;
    if (!rid.isStatic)
    {
        auto processID = [&](Interface* interface)
        {
            if (interface->shutdownFlag.load(std::memory_order_relaxed)) return;
            auto& interfaceInfo = interface->configs;
            tempIp = interfaceInfo.ipv4.getAddress();
            if (tempIp == 0) return;
            if (tempIp < highestIP) return;
            highestIP = tempIp;
        };
        
        {
            std::shared_lock<std::shared_mutex> lock(routingInstance->interfaceMutex);
            for (const auto& [id, interface] : routingInstance->interfaceList)
            {
                if (interface->configs.interfaceType != InterfaceType::LOOPBACK) continue;
                processID(interface);
            }
            if (highestIP == 0)
            {
                for (const auto& [id, interface] : routingInstance->interfaceList)
                {
                    processID(interface);
                }
            }
        }
        writeU32(rid.ID, highestIP);
    }
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

