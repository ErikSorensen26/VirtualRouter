// InterfaceRegistry.cpp

#include "InterfaceRegistry.h"
#include "interface/Interface.h"

namespace config
{
void InterfaceIPAddress(void* ctx)
{
    auto* iface = static_cast<interface::Interface*>(ctx);
    iface->configs.syncPrimaryIP();
}

void InterfaceIPAddressSecondary(void* ctx)
{
    auto* iface = static_cast<interface::Interface*>(ctx);
    iface->configs.syncSecondaryIP();
}
}

