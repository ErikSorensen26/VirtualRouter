// GlobalRegistry.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/registry/router/OspfRegistry.h"
#include "GlobalRegistry.h"

namespace config
{
DEFINE_CONFIG_APPLIER(Global, INTERFACE, g, reg, key)
{
    if (core::Global* global = config::Context::cast<core::Global*>(g); global)
    {
        if (reg)
            global->addInterface(key, *reg);
        else
            global->removeInterface(key);
    }
}

DEFINE_CONFIG_APPLIER(Global, ROUTER_BGP, g, reg, key)
{
    if (core::Global* global = Context::cast<core::Global*>(g); global)
    {
        if (reg)
            global->addBgp(*reg, key);
        else
            global->removeBgp(key);
    }
}

DEFINE_CONFIG_VALIDATOR(Global, ROUTER_BGP, g, key)
{
    if (core::Global* global = Context::cast<core::Global*>(g); global)
    {
        return global->getBgp() == nullptr;
        // TODO add debug message
    }
    return false;
}

DEFINE_CONFIG_APPLIER(Global, ROUTER_EIGRP, g, reg, key)
{
    if (core::Global* global = config::Context::cast<core::Global*>(g); global && reg)
    {
        reg->context().set(global);
        reg->get<config::EigrpNamed::AS>().set(key);
    }
}

DEFINE_CONFIG_APPLIER(Global, ROUTER_OSPFV3, g, reg, key)
{
    if (core::Global* global = config::Context::cast<core::Global*>(g); global && reg)
    {
        reg->context().set(global);
        reg->get<config::Ospf::PROCESS_ID>().set(key);
    }
}

DEFINE_CONFIG_APPLIER(Global, VRF_CONFIGS, g, reg, key)
{
    if (core::Global* global = config::Context::cast<core::Global*>(g); global)
    {
        if (reg)
            global->addRoutingInstance(key, *reg);
        else
            global->removeRoutingInstance(key);
    }
}
}
