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
    IPV4_DOMAIN_LIST, // TODO
    IPV4_DOMAIN_LOOKUP_SOURCE_INTERFACE, // TODO interfacekey
    IPV4_DOMAIN_NAME, // TODO
    IPV4_HOST, // TODO
    IPV4_IGMP_IMMEDIATE_LEAVE_GROUP_LIST, // TODO strings
    IPV4_IGMP_LIMIT, // TODO uint16
    IPV4_IGMP_SSM_MAP, // TODO bool
    IPV4_IGMP_SSM_MAP_QUERY_DNS, // TODO bool
    IPV4_IGMP_SSM_MAP_STATIC, // TODO
    IPV4_MROUTE, // TODO
    IPV4_MSDP, // TODO
    IPV4_MULTICAST, // TODO
    IPV4_MULTICAST_ROUTING, // TODO bool
    IPV4_NAME_SERVER, // TODO ipaddress
    IPV4_PIM, // TODO
    IPV4_RADIUS_SOURCE_INTERFACE, // TODO interfacekey
    IPV4_ROUTE, // TODO
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
    COUNT
};

using VrfRegistry = SubRegistry<Vrf,
    ValueField<std::vector<std::tuple<types::IPv4Address, types::Mac>> CONFIG_INDEX_ARG(Vrf::ARP_STATIC_ENTRY)>
>;
}

#endif // VRF_REGISTRY_HPP
