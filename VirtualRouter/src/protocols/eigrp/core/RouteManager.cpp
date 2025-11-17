// RouteManager.cpp

#include <RouteManager.h>
#include "Eigrp.h"
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <RoutingTable.hpp>
#include <Interface.h>
#include <InterfaceConfigs.h>
#include <HardwareManager.h>

namespace Eigrp
{

RouteManager::RouteManager(Eigrp& process)
    : base(process), rib(process.routingInstance->routingTable)
{
    af = base.getAF();
    as = base.getAS();
}

void RouteManager::withdrawRoute(const IPPrefix withdraw)
{
    if (af == AddressFamily::IPv4)
        rib.removeEntry<uint32_t>(withdraw.v4, withdraw.prefixLength, RouteSource::EIGRP, as);
    else
        rib.removeEntry<__uint128_t>(withdraw.v6, withdraw.prefixLength, RouteSource::EIGRP, as);
}

void RouteManager::withdrawRoutes(const std::vector<IPPrefix>& withdraws)
{
    for (const auto& prefix : withdraws)
        withdrawRoute(prefix);
}

void RouteManager::synchronizeRoutes(const std::vector<TopologyEntry*>& entries, const std::vector<IPPrefix>& withdraws, const std::vector<const RouteInfo*>& individuals)
{
    for (const auto& pfx : withdraws)
        withdrawRoute(pfx);

    uint8_t scale = base.getGlobalConfigMgr().getRibScale();

    std::vector<const RouteInfo*> changedRoutes;

    auto individual = [&]<typename AddrType>(const RouteInfo* entry)
    {
        RibEntry<AddrType> ribEntry;

        ribEntry.addNextHop(
            af == AddressFamily::IPv4 ? entry->routeInfo.nextHop.v4 : entry->routeInfo.nextHop.v6,
            entry->routeInfo.originInterface,
            1
        );
        ribEntry.prefix = af == AddressFamily::IPv4
            ? entry->routeInfo.prefix.v4
            : entry->routeInfo.prefix.v6;
        ribEntry.length = entry->routeInfo.prefix.prefixLength;
        ribEntry.source = RouteSource::EIGRP;
        ribEntry.processId = as;
        ribEntry.adminDistance = entry->routeInfo.adminDistance;
        ribEntry.metric = entry->routeInfo.feasibleDistance * scale;

        if (rib.addRoute<AddrType>(ribEntry))
            changedRoutes.push_back(entry);
    };

    if (af == AddressFamily::IPv4)
    {
        for (const auto& entry : entries)
            if (auto r = syncRoute<uint32_t>(entry, scale); r)
                changedRoutes.push_back(r);
        for (const auto& entry : individuals)
            individual.operator()<uint32_t>(entry);
    }
    else
    {
        for (const auto& entry : entries)
            if (auto r = syncRoute<__uint128_t>(entry, scale); r)
                changedRoutes.push_back(r);
        for (const auto& entry : individuals)
            individual.operator()<__uint128_t>(entry);
    }

    base.broadcastRouteChanges(changedRoutes);
}

void RouteManager::synchronizeRoute(const TopologyEntry& entry)
{
    uint8_t scale = base.getGlobalConfigMgr().getRibScale();
    if (af == AddressFamily::IPv4)
    {
        if (auto r = syncRoute<uint32_t>(&entry, scale); r)
            base.broadcastRouteChanges({r});
    }
    else
    {
        if (auto r = syncRoute<__uint128_t>(&entry, scale); r)
            base.broadcastRouteChanges({r});
    }
}
}
