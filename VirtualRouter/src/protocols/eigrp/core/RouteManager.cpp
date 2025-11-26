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
        rib.removeEntry<uint32_t>(readU32(withdraw.addr), withdraw.prefixLength, RouteSource::EIGRP, as);
    else
        rib.removeEntry<__uint128_t>(readU128(withdraw.addr), withdraw.prefixLength, RouteSource::EIGRP, as);
}

void RouteManager::withdrawRoutes(const std::vector<IPPrefix>& withdraws)
{
    for (const auto& prefix : withdraws)
        withdrawRoute(prefix);
}

void RouteManager::synchronizeRoutes(const std::vector<TopologyEntry*>& entries)
{
    uint8_t scale = base.getGlobalConfigMgr().getRibScale();

    std::vector<const RouteInfo*> changedRoutes;

    if (af == AddressFamily::IPv4)
    {
        for (const auto& entry : entries)
            if (auto r = syncRoute<uint32_t>(entry, scale); r)
                changedRoutes.push_back(r);
    }
    else
    {
        for (const auto& entry : entries)
            if (auto r = syncRoute<__uint128_t>(entry, scale); r)
                changedRoutes.push_back(r);
    }

    if (!changedRoutes.empty())
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
