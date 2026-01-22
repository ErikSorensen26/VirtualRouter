// InterfaceIPv6Helpers.hpp

#ifndef INTERFACE_HELPERS_HPP
#define INTERFACE_HELPERS_HPP

#include <Interface.h>
#include <AddressFamily.hpp>
#include <EigrpTypes.hpp>

namespace Cli
{
inline void refreshEigrpConfig(Interface& iface, uint32_t as, AddressFamily af, EigrpConfigs::InterfaceConfigs* config)
{
    if (!iface.eigrpInterfaceList.contains(as) && config->isDefault())
    {
        delete config;
        iface.configs.eigrp.eigrpInterfaceConfigList.erase({as, af});
    }
}
}

#endif // INTERFACE_HELPERS_HPP
