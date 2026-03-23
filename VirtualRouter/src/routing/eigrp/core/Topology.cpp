// EigrpTopology.cpp

#include "Topology.h"
#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "hardware/HardwareManager.h"

namespace routing::eigrp
{
EigrpTopology::EigrpTopology(Eigrp& base) : duel(base), base(base) {}

void EigrpTopology::pruneStaleRoutes()
{
    duel.topologyTable.pruneExpired();
}

std::unordered_map<types::IPPrefix, TopologyEntry>& EigrpTopology::entries()
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
    types::IPAddress connected = (base.getAF() == types::AddressFamily::IPv4) ? types::IPAddress(uint32_t(0)) : types::IPAddress(__uint128_t(0));

    ReceivedRoute r{};
    r.originInterface = iface.interfaceKey;
    r.bandwidth = base.isNamed()
        ? interface->configs.hwInfo.bandwidth
        : interface->configs.bandwidth.load(std::memory_order_relaxed);
    r.delay = 0;
    r.load = interface->configs.load.load(std::memory_order_relaxed);
    r.reliability = interface->configs.reliability.load(std::memory_order_relaxed);
    r.hopCount = 0;
    r.mtu = base.getAF() == types::AddressFamily::IPv4
        ? interface->configs.ipv4.mtu.load(std::memory_order_relaxed)
        : interface->configs.ipv6.mtu.load(std::memory_order_relaxed);
    r.routeType = RouteType::CONNECTED;
    r.adminDistance = base.getGlobalConfigMgr().getAD();
    r.nextHop = connected; // Self originated

    std::set<types::IPPrefix> withdraws = iface.connectedRoutes;
    iface.connectedRoutes.clear();
    std::vector<TopologyEntry*> updates;

    auto install = [&](types::IPPrefix prefix)
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

    if (base.getAF() == types::AddressFamily::IPv4)
    {
        if (interface->configs.ipv4.hasPrimaryAddress())
        {
            types::IPv4Address v4addr = interface->configs.ipv4.getPrimaryAddress();
            uint8_t mask = interface->configs.ipv4.getPrimaryMask();
            types::IPPrefix prefix(v4addr.addr, mask);
            install(types::IPPrefix(prefix.addr, prefix.prefixLength));
        }
    }
    else
    {
        for (const auto& prefix : interface->configs.ipv6.getGlobalPrefixList())
            install(types::IPPrefix(prefix.addr, prefix.prefixLength));
        for (const auto& prefix : interface->configs.ipv6.getLocalPrefixList())
            install(types::IPPrefix(prefix.addr, prefix.prefixLength));
    }

    // Remove left over routes
    for (const auto& route : withdraws)
    {
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
    types::IPAddress connected = (base.getAF() == types::AddressFamily::IPv4) ? types::IPAddress(uint32_t(0)) : types::IPAddress(__uint128_t(0));
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
} // namespace routing
