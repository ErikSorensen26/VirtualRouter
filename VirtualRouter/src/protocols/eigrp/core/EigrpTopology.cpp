// EigrpTopology.cpp

#include "EigrpTopology.h"
#include "Eigrp.h"
#include <EigrpInterface.h>
#include <Interface.h>
#include <InterfaceConfigs.h>
#include <HardwareManager.h>

namespace Eigrp
{
EigrpTopology::EigrpTopology(Eigrp& base) : duel(base), base(base) {}

void EigrpTopology::pruneStaleRoutes()
{
    duel.topologyTable.pruneExpired();
}

std::unordered_map<IPPrefix, TopologyEntry*>& EigrpTopology::entries()
{
    return duel.topologyTable.entries();
}

void EigrpTopology::recalculateAll()
{
    duel.recalculateAllRoutes();
}

void EigrpTopology::handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor)
{
    duel.handleSIATimeout(query, neighbor);
}

void EigrpTopology::synchronizeConnected(EigrpInterface& iface)
{
    const auto* interface = iface.getIface();
    IPAddress connected = IPAddress(base.getAF());

    ReceivedRoute r;
    r.originInterface = iface.interfaceKey;
    r.bandwidth = base.isNamed()
        ? interface->configs.hwInfo.bandwidth
        : interface->configs.bandwidth.load(std::memory_order_relaxed);
    r.delay = interface->configs.delay.load(std::memory_order_relaxed) * 10'000'000;
    r.load = interface->configs.load.load(std::memory_order_relaxed);
    r.load = interface->configs.reliability.load(std::memory_order_relaxed);
    r.hopCount = 0;
    r.mtu = base.getAF() == AddressFamily::IPv4
        ? interface->configs.ipv4.mtu.load(std::memory_order_relaxed)
        : interface->configs.ipv6.mtu.load(std::memory_order_relaxed);
    r.routeType = RouteType::CONNECTED;
    r.adminDistance = base.getGlobalConfigMgr().getAD();
    r.nextHop = connected; // Self originated

    std::set<IPPrefix> withdraws = iface.connectedRoutes;
    std::vector<TopologyEntry*> updates;

    auto install = [&](IPPrefix prefix)
    {
        ReceivedRoute newRoute = r;
        newRoute.prefix = prefix;
        auto& entry = duel.topologyTable.ensure(newRoute.prefix);
        base.getAggregator().updateSummary(entry);
        duel.topologyTable.addRouteUpdate(newRoute, nullptr, entry);
        withdraws.erase(prefix); // Erase to mark found
        iface.connectedRoutes.insert(prefix);
        updates.push_back(&entry);
    };

    if (base.getAF() == AddressFamily::IPv4)
    {
        IPPrefix prefix;
        if (interface->configs.ipv4.getAddress(prefix.addr))
        {
            prefix.addPrefixLen(interface->configs.ipv4.getMask());
            prefix.af = AddressFamily::IPv4;
            install(prefix);
        }
    }
    else
    {
        for (const auto& prefix : interface->configs.ipv6.getGlobalPrefixList())
            install(prefix);
        for (const auto& prefix : interface->configs.ipv6.getLocalPrefixList())
            install(prefix);
    }

    // Remove left over routes
    for (const auto& route : withdraws)
    {
        iface.connectedRoutes.erase(route);
        if (auto* entry = duel.topologyTable.find(route); entry)
            if (auto rit = entry->routesBySource.find(connected); rit != entry->routesBySource.end())
            {
                duel.topologyTable.markRouteUnreachable(rit->second, connected, *entry);
                updates.push_back(entry);
            }
    }

    duel.updateSuccessors(updates);
}

void EigrpTopology::clearConnected(EigrpInterface& iface)
{
    std::vector<TopologyEntry*> updates;
    IPAddress connected = IPAddress(base.getAF());
    for (auto it = iface.connectedRoutes.begin(); it != iface.connectedRoutes.end();)
    {
        if (auto* entry = duel.topologyTable.find(*it); entry)
        {
            if (auto rit = entry->routesBySource.find(connected); rit != entry->routesBySource.end())
            {
                duel.topologyTable.markRouteUnreachable(rit->second, connected, *entry);
                updates.push_back(entry);
            }
        }
        it = iface.connectedRoutes.erase(it);
    }

    duel.updateSuccessors(updates);
    base.routeManager.synchronizeRoutes(updates);
}
}
