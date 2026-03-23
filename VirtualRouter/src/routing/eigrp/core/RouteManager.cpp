// RouteManager.cpp

#include <VirtualRouter.h>

#include "RouteManager.h"
#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "routing/RoutingTable.hpp"
#include "interface/Interface.h"

namespace routing::eigrp
{
RouteManager::RouteManager(Eigrp& process)
    : base(process), rib(process.routingInstance->getRib())
{
    af = base.getAF();
    as = base.getAS();
}

void RouteManager::withdrawRoute(const types::IPPrefix withdraw)
{
    if (af == types::AddressFamily::IPv4)
    {
        rib.removeRoute<uint32_t>(withdraw.v4(), withdraw.prefixLength, core::RouteSource::EIGRP_INTERNAL, as);
        rib.removeRoute<uint32_t>(withdraw.v4(), withdraw.prefixLength, core::RouteSource::EIGRP_EXTERNAL, as);
    }
    else
    {
        rib.removeRoute<__uint128_t>(withdraw.v6(), withdraw.prefixLength, core::RouteSource::EIGRP_INTERNAL, as);
        rib.removeRoute<__uint128_t>(withdraw.v6(), withdraw.prefixLength, core::RouteSource::EIGRP_EXTERNAL, as);
    }
}

void RouteManager::synchronizeRoutes(const std::vector<TopologyEntry*>& entries)
{
    uint8_t scale = base.getGlobalConfigMgr().getRibScale();

    std::vector<const RouteInfo*> changedRoutes;

    if (af == types::AddressFamily::IPv4)
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
    if (af == types::AddressFamily::IPv4)
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
} // namespace routing
