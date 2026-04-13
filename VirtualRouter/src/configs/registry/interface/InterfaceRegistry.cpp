// InterfaceRegistry.cpp

#include <VirtualRouter.h>
#include "InterfaceRegistry.h"
#include "interface/Interface.h"

namespace config
{
void interfaceIPAddress(void* ctx)
{
    auto* iface = static_cast<interface::Interface*>(ctx);
    iface->configs.syncPrimaryIP();
}

void interfaceIPAddressSecondary(void* ctx)
{
    auto* iface = static_cast<interface::Interface*>(ctx);
    iface->configs.syncSecondaryIP();
}

void interfaceShutdown(void* i)
{
    interface::Interface& iface = *static_cast<interface::Interface*>(i);
    iface.syncShutdown();
}

void interfaceIPv6Eigrp(void* i)
{
    interface::Interface& iface = *static_cast<interface::Interface*>(i);
    iface.getVRF()->refreshEigrpV6Interfaces();
}
}

