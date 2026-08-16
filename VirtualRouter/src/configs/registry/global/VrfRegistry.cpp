// VrfRegistry.cpp

#include <VirtualRouter.h>
#include <Global.h>
#include "VrfRegistry.h"

namespace config
{
DEFINE_CONFIG_APPLIER(Vrf, ROUTER_OSPF, v, o, id)
{
    if (core::VirtualRouter* vrf = Context::cast<core::VirtualRouter*>(v); vrf)
    {
        if (o)
            vrf->addOspf(*o, id);
        else
            vrf->removeOspf(id);
    }
}
}
