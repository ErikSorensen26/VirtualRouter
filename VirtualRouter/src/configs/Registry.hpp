// Registry.hpp

#ifndef REGISTRY_HPP
#define REGISTRY_HPP

#include <RegistryDatabase.hpp>

#include <OspfInterfaceRegistry.hpp>
#include <OspfRegistry.hpp>

namespace Config
{
using Registry = RegistryDatabase<
    OspfRegistry,
    OspfRegistryMask,
    OspfTopologyBaseRegistry,
    OspfTopologyRegistry,
    OspfAddressFamilyV2Registry,
    OspfAddressFamilyV3Registry,
    OspfInterfaceRegistry,
    OspfInterfaceRegistryMask,
    OspfInterfaceBaseRegistry,
    OspfInterfaceAddressFamilyRegistry
>;
}

#endif // REGISTRY_HPP
