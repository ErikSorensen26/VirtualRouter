// InterfaceIPv6Helpers.hpp

#ifndef INTERFACE_HELPERS_HPP
#define INTERFACE_HELPERS_HPP

#include <AddressFamily.hpp>

#include "eigrp/EigrpTypes.hpp"
#include "interface/Interface.h"

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
