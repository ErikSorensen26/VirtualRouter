// Registry.hpp

#ifndef REGISTRY_HPP
#define REGISTRY_HPP

#include "RegistryDatabase.hpp"
#include "SubRegistry.hpp" // KEEP

#include <OspfInterfaceRegistry.h>
#include <OspfRegistry.h>

namespace Config
{
using Registry = RegistryDatabase<
    OspfRegistry,
    OspfAddressFamilyV2Registry,
    OspfAddressFamilyV3Registry,
    OspfInterfaceRegistry,
    OspfInterfaceBaseRegistry,
    OspfInterfaceIPSecRegistry,
    OspfInterfaceAddressFamilyRegistry
>;
}

#endif // REGISTRY_HPP
