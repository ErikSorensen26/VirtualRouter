// VirtualLink.cpp

#include <algorithm>

#include <VirtualRouter.h>

#include "VirtualLink.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"
#include "ospf/interface/InterfaceManager.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/topology/RouteManagerUtility.h"
#include "interface/Interface.h"

namespace routing::ospf
{
VirtualLink::VirtualLink(OspfProcess& proc, const OspfInterfaceId& id, uint32_t transitArea, const config::OspfVirtualLinkRegistry& cfgs)
    : OspfInterfaceBase(proc, id, cfgs),
      transitAreaId(transitArea),
      remoteRouterId(id.interfaceId),
      configs(cfgs)
{
    configs.context().set(static_cast<OspfInterfaceBase*>(this));
    configsBase.context().set(static_cast<OspfInterfaceBase*>(this));
    globalConfigsBase.context().set(static_cast<OspfInterfaceBase*>(this));
    syncConfigs();
    tmgr.startHello();
}

OspfInterface* VirtualLink::resolveTransitPath() const
{
    Area* transit = process.getArea(transitAreaId);
    if (!transit)
    {
        transmitAddress = types::IPPrefix();
        transmitCost = 0;
        return nullptr;
    }

    auto resolved = RouteManagerUtility::resolveVirtualLinkPath(*transit, remoteRouterId);
    if (!resolved)
    {
        transmitAddress = types::IPPrefix();
        transmitCost = 0;
        return nullptr;
    }

    auto& [cost, nextHop] = *resolved;

    const OspfInterfaceBase* firstHop = process.ifaceMgr.getInterface({nextHop.interfaceId, transitAreaId});
    if (!firstHop)
    {
        transmitAddress = types::IPPrefix();
        transmitCost = 0;
        return nullptr;
    }

    transmitAddress = types::IPPrefix(nextHop.nextHop, process.isV3 ? 128 : 32, true);
    transmitCost = static_cast<uint16_t>(std::min<uint64_t>(cost, 0xFFFF));

    return const_cast<OspfInterface*>(static_cast<const OspfInterface*>(firstHop));
}

interface::Interface* VirtualLink::getTransmitInterface() const
{
    OspfInterface* firstHop = resolveTransitPath();
    if (!firstHop)
        return nullptr;

    return &firstHop->iface;
}

std::optional<OspfNextHop> VirtualLink::getRoutingNextHop() const
{
    OspfInterface* firstHop = resolveTransitPath();
    if (!firstHop)
        return std::nullopt;

    return OspfNextHop{firstHop->interfaceId, types::IPAddress(transmitAddress, process.isV3 ? 128 : 32)};
}
}
