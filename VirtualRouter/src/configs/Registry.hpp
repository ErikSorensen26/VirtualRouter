// Registry.hpp

#ifndef REGISTRY_HPP
#define REGISTRY_HPP

#include "registry/router/OspfInterfaceRegistry.h"
#include "registry/router/OspfRegistry.h"
#include "registry/router/BgpRegistry.h"

#include "RegistryDatabase.hpp"

namespace Config
{
using Registry = RegistryDatabase<
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
    BgpNeighborRegistry
>;
}

#endif // REGISTRY_HPP
