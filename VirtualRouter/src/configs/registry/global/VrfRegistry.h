/**
 * @file VrfRegistry.h
 * @brief Vrf configuration registry
 *
 * Defines the configuration schema for the global scope.
 */

#ifndef VRF_REGISTRY_HPP
#define VRF_REGISTRY_HPP

#include <IPAddress.h>
#include <Mac.hpp>

#include "configs/RegistryTypes.hpp"
#include "configs/SubRegistry.hpp"

namespace config
{
enum class Vrf2
{
    ARP_STATIC_ENTRY,
    ROUTER_BGP, // TODO uint32 reference
    ROUTER_EIGRP_CLASSIC, // TODO uint16 reference
    ROUTER_EIGRP_NAMED, // TODO string reference
    ROUTER_OSPF, // TODO uitn32 reference
    ROUTER_OSPFV3, // TODO uint32 reference
    ROUTER_RIP, // TODO reference
    IP_DOMAIN_LIST, // TODO
    IP_DOMAIN_LOOKUP_SOURCE_INTERFACE, // TODO interfacekey
    IP_DOMAIN_NAME, // TODO
    IP_HOST, // TODO
    IP_IGMP_IMMEDIATE_LEAVE_GROUP_LIST, // TODO strings
    IP_IGMP_LIMIT, // TODO uint16
    IP_IGMP_SSM_MAP, // TODO bool
    IP_IGMP_SSM_MAP_QUERY_DNS, // TODO bool
    IP_IGMP_SSM_MAP_STATIC, // TODO
    IP_MROUTE, // TODO
    IP_MSDP, // TODO
    IP_MULTICAST, // TODO
    IP_MULTICAST_ROUTING, // TODO bool
    IP_NAME_SERVER, // TODO ipaddress
    IP_PIM, // TODO
    IP_RADIUS_SOURCE_INTERFACE, // TODO interfacekey
    IP_ROUTE, // TODO
    IPV6_MLD_SSM_MAP, // TODO bool
    IPV6_MLD_SSM_MAP_QUERY_DNS, // TODO bool
    IPV6_MLD_SSM_MAP_STATIC, // TODO
    IPV6_MLD_STATE_LIMIT, //TODO uint16
    IPV6_MULTICAST, // TODO
    IPV6_MULTICAST_ROUTING, // TODO bool
    IPV6_PIM, // TODO
    IPV6_ROUTE, // TODO
    IPV6_ROUTER_EIGRP, // TODO uint16 reference
    IPV6_ROUTER_OSPF, // TODO uint16 reference
    IPV6_ROUTER_RIB, // TODO string reference
};

enum class Vrf
{
    ARP_STATIC_ENTRY,
};

using VrfRegistry = SubRegistry<Vrf,
    ValueField<std::vector<types::IPv4Address, types::Mac>>
>;
}

#endif // VRF_REGISTRY_HPP
