/**
 * @file InterfaceRegistry.h
 * @brief Interface configuration registry
 *
 * Defines the configuration schema for a Interface including...
 */

#ifndef INTERFACE_REGISTRY_H
#define INTERFACE_REGISTRY_H

#include <IPAddress.h>
#include <Mac.hpp>

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/SubRegistry.hpp"

#include "ArpRegistry.h"
#include "NdpRegistry.h"

namespace config
{
enum class Interface
{
    ARP,
    IP_ADDRESS,
    IP_ADDRESS_DHCP,
    IP_ADDRESS_POOL,
    IPV6_ND,
};

#define INTERFACE_DEFAULTS(X) \
    X(Interface, IP_ADDRESS_DHCP, false) \
    X

using InterfaceRegistry = SubRegistry<Interface,
    ReferenceContainer<ArpRegistry CONFIG_INDEX_ARG(Interface::ARP)>,
    OptionalAtomicField<types::IPv4Address CONFIG_INDEX_ARG(Interface::IP_ADDRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ADDRESS_DHCP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Interface::IP_ADDRESS_POOL)>,
    ReferenceContainer<NdpRegistry CONFIG_INDEX_ARG(Interface::IPV6_ND)>
>;
}

#endif // INTERFACE_REGISTRY_H
