// OspfRegistry.cpp

#include "OspfRegistry.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"
#include "Global.h"
#include "VirtualRouter.h"

namespace config
{
DEFINE_CONFIG_APPLIER(OspfArea, AREA_TYPE, ctx, newType)
{
    if (routing::ospf::Area* area = Context::cast<routing::ospf::Area*>(ctx); area)
        area->enqueueReset(newType);
}

DEFINE_CONFIG_APPLIER(OspfArea, RANGE, ctx, range, add)
{
    if (routing::ospf::Area* area = Context::cast<routing::ospf::Area*>(ctx); area)
        area->enqueueSyncRanges();
}

DEFINE_CONFIG_APPLIER(Ospf, NEIGHBORS, ctx, nbr, add)
{
    if (routing::ospf::OspfProcess* base = Context::cast<routing::ospf::OspfProcess*>(ctx); base)
        base->enqueueSyncNeighbor();
}

DEFINE_CONFIG_APPLIER(Ospf, NETWORKS, ctx, net, add)
{
    if (routing::ospf::OspfProcess* base = Context::cast<routing::ospf::OspfProcess*>(ctx); base)
        base->enqueueSyncNetworks();
}

DEFINE_CONFIG_APPLIER(Ospf, SUMMARY_ADDRESS, ctx, sum, add)
{
    if (routing::ospf::OspfProcess* base = Context::cast<routing::ospf::OspfProcess*>(ctx); base)
        base->enqueueSyncSummaries();
}

DEFINE_CONFIG_APPLIER(Ospf, IPV4_INSTANCES, ctx, reg, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global)
    {
        core::VirtualRouter* vrf = global->getRoutingInstance(key);
        if (reg)
        {
            vrf->addOspfv3(
                *reg, reg->resolveParent<OspfRegistry>()->get<config::Ospf::PROCESS_ID>().load(),
                types::AddressFamily::IPv4);
        }
        else
        {
            vrf->removeOspfv3(
                reg->resolveParent<OspfRegistry>()->get<config::Ospf::PROCESS_ID>().load(),
                types::AddressFamily::IPv4);
        }
    }
}

DEFINE_CONFIG_APPLIER(Ospf, IPV6_INSTANCES, ctx, reg, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global)
    {
        core::VirtualRouter* vrf = global->getRoutingInstance(key);
        if (reg)
        {
            vrf->addOspfv3(
                *reg, reg->resolveParent<OspfRegistry>()->get<config::Ospf::PROCESS_ID>().load(),
                types::AddressFamily::IPv6);
        }
        else
        {
            vrf->removeOspfv3(
                reg->resolveParent<OspfRegistry>()->get<config::Ospf::PROCESS_ID>().load(),
                types::AddressFamily::IPv6);
        }
    }
}

DEFINE_CONFIG_VALIDATOR(Ospf, IPV4_INSTANCES, ctx, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global) 
    {
        if (global->getRoutingInstance(key))
            return true;
        // TODO add warning
    }
    return false;
}

DEFINE_CONFIG_VALIDATOR(Ospf, IPV6_INSTANCES, ctx, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global) 
    {
        if (global->getRoutingInstance(key))
            return true;
        // TODO add warning
    }
    return false;
}
}
