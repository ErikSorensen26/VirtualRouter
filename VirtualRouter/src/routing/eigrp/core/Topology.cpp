// EigrpTopology.cpp

#include "Topology.h"
#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "hardware/HardwareManager.h"
#include "interface/configs/InterfaceType.hpp"

namespace routing::eigrp
{
EigrpTopology::EigrpTopology(Eigrp& process) : dual(process), process(process) {}

void EigrpTopology::pruneStaleRoutes()
{
    dual.topologyTable.pruneExpired();
}

std::unordered_map<types::IPPrefix, TopologyEntry>& EigrpTopology::entries()
{
    return dual.topologyTable.entries();
}

void EigrpTopology::recalculateAll()
{
    dual.recalculateAllRoutes();
}

void EigrpTopology::handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor)
{
    dual.handleSIATimeout(query, neighbor);
}

void EigrpTopology::synchronizeConnected(EigrpInterface& iface)
{
    const auto* interface = iface.currentInterface;
    types::IPAddress connected = (process.addressFamily == types::AddressFamily::IPv4) ? types::IPAddress(uint32_t(0)) : types::IPAddress(__uint128_t(0));

    ReceivedRoute r{};
    r.originInterface = iface.interfaceKey;
    r.bandwidth = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    r.delay = 0;
    r.load = 1;
    r.reliability = 255;
    r.hopCount = 0;
    r.mtu = process.addressFamily == types::AddressFamily::IPv4
        ? interface->configs.ipv4.mtu.load(std::memory_order_relaxed)
        : interface->configs.ipv6.mtu.load(std::memory_order_relaxed);
    r.routeType = RouteType::CONNECTED;
    r.adminDistance = process.getConfigs().get<config::Eigrp::INTERNAL_ADMIN_DISTANCE>().load();
    r.nextHop = connected; // Self originated

    std::set<types::IPPrefix> withdraws = iface.connectedRoutes;
    iface.connectedRoutes.clear();
    std::vector<TopologyEntry*> updates;

    auto install = [&](types::IPPrefix prefix)
    {
        ReceivedRoute newRoute = r;
        newRoute.prefix = prefix;
        auto& entry = dual.topologyTable.ensure(newRoute.prefix);
        process.getAggregator().updateSummary(entry);
        dual.topologyTable.addRouteUpdate(newRoute, nullptr, entry);
        withdraws.erase(prefix); // Erase to mark found
        iface.connectedRoutes.insert(prefix);
        updates.push_back(&entry);
    };

    if (process.addressFamily == types::AddressFamily::IPv4)
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
        if (auto* entry = dual.topologyTable.find(route); entry)
            if (auto rit = entry->routesBySource.find(connected); rit != entry->routesBySource.end())
            {
                dual.topologyTable.markRouteUnreachable(rit->second, connected, *entry);
                updates.push_back(entry);
            }
    }

    dual.updateSuccessors(updates);
}

void EigrpTopology::clearConnected(EigrpInterface& iface)
{
    std::vector<TopologyEntry*> updates;
    types::IPAddress connected = (process.addressFamily == types::AddressFamily::IPv4) ? types::IPAddress(uint32_t(0)) : types::IPAddress(__uint128_t(0));
    for (auto it = iface.connectedRoutes.begin(); it != iface.connectedRoutes.end();)
    {
        if (auto* entry = dual.topologyTable.find(*it); entry)
        {
            if (auto rit = entry->routesBySource.find(connected); rit != entry->routesBySource.end())
            {
                dual.topologyTable.markRouteUnreachable(rit->second, connected, *entry);
                updates.push_back(entry);
            }
        }
        it = iface.connectedRoutes.erase(it);
    }

    dual.updateSuccessors(updates);
    process.routeManager.synchronizeRoutes(updates);
}
} // namespace routing
