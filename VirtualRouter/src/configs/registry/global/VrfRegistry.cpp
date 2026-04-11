// VrfRegistry.cpp

#include <VirtualRouter.h>
#include "VrfRegistry.h"

namespace config
{
void VrfRouterEigrpV4(void* v)
{
    core::VirtualRouter& vrf = *static_cast<core::VirtualRouter*>(v);
    vrf.refreshEigrpV4();
}

void VrfRouterEigrpV6(void* v)
{
    core::VirtualRouter& vrf = *static_cast<core::VirtualRouter*>(v);
    vrf.refreshEigrpV6();
}
}
