// EigrpRegistry.cpp

#include "EigrpRegistry.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "Global.h"
#include "VirtualRouter.h"

namespace config
{
DEFINE_CONFIG_APPLIER(Eigrp, AUTO_SUMMARIZATION, ctx, sum)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
        eigrp->enqueueSetAutoSummarization(sum);
}

DEFINE_CONFIG_APPLIER(Eigrp, AF_INTERFACE, ctx, reg, key)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
    {
        if (reg && !reg->isMasked())
            reg->setMask(&reg->resolveParent<EigrpRegistry>()->get<config::Eigrp::AF_INTERFACE_DEFAULT>().get());
        eigrp->enqueueSetAfInterface(key, reg);
    }
}

DEFINE_CONFIG_APPLIER(Eigrp, SHUTDOWN, ctx, shut)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
        eigrp->enqueueSetShutdown(shut);
}

DEFINE_CONFIG_APPLIER(Eigrp, VARIANCE, ctx,)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
        eigrp->enqueueSyncTopology();
}

DEFINE_CONFIG_APPLIER(Eigrp, WEIGHTS, ctx,)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
        eigrp->enqueueSyncTopology();
}

DEFINE_CONFIG_APPLIER(Eigrp, NEIGHBOR, ctx, reg, ip)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp && reg)
    {
        reg->context().set(&eigrp->getNeighborContext(ip));
    }
}

DEFINE_CONFIG_APPLIER(EigrpNeighbor, INTERFACE, ctx, key)
{
    if (routing::eigrp::EigrpNeighborContext* nbrCtx = Context::cast<routing::eigrp::EigrpNeighborContext*>(ctx); nbrCtx)
        nbrCtx->eigrp.enqueueSetUnicastNeighbor(nbrCtx->addr, key);
}

DEFINE_CONFIG_APPLIER(Eigrp, PASSIVE_INTERFACES, ctx, key, add)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
        eigrp->enqueueSetPassive(key, add);
}

DEFINE_CONFIG_APPLIER(Eigrp, ROUTER_ID, ctx, id)
{
    if (routing::eigrp::Eigrp* eigrp = Context::cast<routing::eigrp::Eigrp*>(ctx); eigrp)
        eigrp->enqueueSetRouterId(id ? std::optional<uint32_t>(*id) : std::nullopt);
}

DEFINE_CONFIG_APPLIER(EigrpNamed, V4_INSTANCES, ctx, reg, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global)
    {
        core::VirtualRouter* vrf = global->getRoutingInstance(key);
        if (reg)
        {
            vrf->addEigrpAutonomousSystem(
                *reg, reg->resolveParent<EigrpNamedRegistry>()->get<config::EigrpNamed::AS>().load(),
                types::AddressFamily::IPv4);
        }
        else
        {
            vrf->removeEigrpAutonomousSystem(
                reg->resolveParent<EigrpNamedRegistry>()->get<config::EigrpNamed::AS>().load(),
                types::AddressFamily::IPv4);
        }
    }
}

DEFINE_CONFIG_APPLIER(EigrpNamed, V6_INSTANCES, ctx, reg, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global)
    {
        core::VirtualRouter* vrf = global->getRoutingInstance(key);
        if (reg)
        {
            vrf->addEigrpAutonomousSystem(
                *reg, reg->resolveParent<EigrpNamedRegistry>()->get<config::EigrpNamed::AS>().load(),
                types::AddressFamily::IPv4);
        }
        else
        {
            vrf->removeEigrpAutonomousSystem(
                reg->resolveParent<EigrpNamedRegistry>()->get<config::EigrpNamed::AS>().load(),
                types::AddressFamily::IPv4);
        }
    }
}

DEFINE_CONFIG_VALIDATOR(EigrpNamed, V6_INSTANCES, ctx, key)
{
    if (core::Global* global = Context::cast<core::Global*>(ctx); global) 
    {
        if (global->getRoutingInstance(key))
            return true;
        // TODO add warning
    }
    return false;
}

DEFINE_CONFIG_VALIDATOR(EigrpNamed, V4_INSTANCES, ctx, key)
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
