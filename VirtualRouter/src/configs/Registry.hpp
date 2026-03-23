// Registry.hpp

#ifndef REGISTRY_HPP
#define REGISTRY_HPP

#include "registry/router/EigrpInterfaceRegistry.h"
#include "registry/router/EigrpRegistry.h"
#include "registry/router/OspfInterfaceRegistry.h"
#include "registry/router/OspfRegistry.h"
#include "registry/router/BgpRegistry.h"

#include "RegistryDatabase.hpp"

namespace config
{
using Registry = RegistryDatabase<
    EigrpRegistry,
    EigrpInterfaceRegistry,
    OspfRegistry,
    OspfAreaRegistry,
    OspfAddressFamilyV2Registry,
    OspfAddressFamilyV3Registry,
    OspfInterfaceRegistry,
    OspfInterfaceBaseRegistry,
    OspfInterfaceIPSecRegistry,
    OspfInterfaceAddressFamilyRegistry,
    BgpRegistry,
    BgpBaseRegistry,
    BgpAfBaseRegistry,
    BgpNeighborSessionRegistry,
    BgpNeighborRegistry
>;
}

#endif // REGISTRY_HPP
