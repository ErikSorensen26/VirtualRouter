// Registry.hpp

#ifndef REGISTRY_HPP
#define REGISTRY_HPP

#include <RegistryDatabase.hpp>

#include <OspfInterfaceRegistry.hpp>

namespace Config
{
using Registry = RegistryDatabase<
    OspfInterfaceRegistry,
    OspfInterfaceRegistryMask,
    OspfInterfaceBaseRegistry,
    OspfInterfaceAddressFamilyRegistry
>;
}

#endif // REGISTRY_HPP
