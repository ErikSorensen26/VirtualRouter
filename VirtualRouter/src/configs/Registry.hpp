/**
 * @file Registry.hpp
 * @brief Concrete `Registry` type alias listing all protocol config entry types.
 */

#ifndef REGISTRY_HPP
#define REGISTRY_HPP

#include "registry/router/EigrpInterfaceRegistry.h"
#include "registry/router/EigrpRegistry.h"
#include "registry/router/OspfInterfaceRegistry.h"
#include "registry/router/OspfRegistry.h"
#include "registry/router/BgpRegistry.h"
#include "registry/global/GlobalRegistry.h"
#include "registry/interface/InterfaceRegistry.h"

#include "RegistryDatabase.hpp"

/**
 * @brief Typed configuration registry for all protocol scopes.
 *
 * See RegistryDatabase.hpp for the full namespace description.
 */
namespace config
{
/**
 * @brief The process-wide configuration registry holding all protocol scopes.
 * @ingroup CONFIG
 *
 * `Registry` is the single concrete instantiation of @ref RegistryDatabase
 * that the CLI engine constructs at startup. Every EIGRP AS, OSPF area, BGP
 * neighbor, etc. is stored here as a reference-counted slot.
 *
 * Protocols receive a @ref Reference into this registry at construction time
 * and hold it for their own lifetime.
 *
 * @see RegistryDatabase
 */
using Registry = RegistryDatabase<
    GlobalRegistry,
    VrfRegistry,
    InterfaceRegistry,
    ArpRegistry,
    NdpRegistry,
    NdpBaseRegistry,
    NdpEntryRegistry,

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
