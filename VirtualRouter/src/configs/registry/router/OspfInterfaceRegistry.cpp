// OspfInterfaceRegistry.cpp

#include "OspfInterfaceRegistry.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"

namespace config
{
DEFINE_CONFIG_APPLIER(OspfInterfaceBase, DEAD_INTERVAL, ctx, dead)
{
    if (routing::ospf::OspfInterfaceBase* iface = Context::cast<routing::ospf::OspfInterfaceBase*>(ctx); iface)
        iface->enqueueSyncTimers();
}

DEFINE_CONFIG_APPLIER(OspfInterfaceBase, HELLO_INTERVAL, ctx, hello)
{
    if (routing::ospf::OspfInterfaceBase* iface = Context::cast<routing::ospf::OspfInterfaceBase*>(ctx); iface)
        iface->enqueueSyncTimers();
}

DEFINE_CONFIG_APPLIER(OspfInterfaceBase, HELLO_MULTIPLIER, ctx, multiplier)
{
    if (routing::ospf::OspfInterfaceBase* iface = Context::cast<routing::ospf::OspfInterfaceBase*>(ctx); iface)
        iface->enqueueSyncTimers();
}

DEFINE_CONFIG_APPLIER(OspfInterface, NEIGHBOR, ctx, nbr, add)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncUnicastNeighbors();
}

DEFINE_CONFIG_APPLIER(OspfInterface, NETWORK, ctx, ntype)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncNetworkType(ntype);
}

DEFINE_CONFIG_APPLIER(OspfInterface, DEMAND_CIRCUIT, ctx, demand)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncDemandCircuit();
}

DEFINE_CONFIG_APPLIER(OspfInterface, DEMAND_CIRCUIT_IGNORE, ctx, ignore)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncDemandCircuit();
}

DEFINE_CONFIG_APPLIER(OspfInterface, FLOOD_REDUCTION, ctx, reduce)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncDemandCircuit();
}

DEFINE_CONFIG_APPLIER(OspfInterface, PASSIVE, ctx, passive)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncPassive(passive);
}

DEFINE_CONFIG_APPLIER(OspfGlobalInterfaceBase, MESSAGE_DIGEST_KEYS, ctx,,)
{
    if (routing::ospf::OspfInterfaceBase* iface = Context::cast<routing::ospf::OspfInterfaceBase*>(ctx); iface)
        iface->enqueueSyncDigestKey();
}

DEFINE_CONFIG_APPLIER(OspfGlobalInterface, PREFIX_SUPPRESSION, ctx, suppress)
{
    if (routing::ospf::OspfInterface* iface = Context::cast<routing::ospf::OspfInterface*>(ctx); iface)
        iface->enqueueSyncPrefixSuppression();
}
}
