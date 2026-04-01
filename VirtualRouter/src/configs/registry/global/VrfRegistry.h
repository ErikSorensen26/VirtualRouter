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

#include "configs/registry/router/BgpRegistry.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/registry/router/OspfRegistry.h"

struct Incomplete {};
enum class Empty { COUNT };
using EmptyRegistry = config::SubRegistry<Empty>;

namespace config
{
enum class Vrf
{
    ARP_STATIC_ENTRY,
    ROUTER_BGP_AS, // TODO
    ROUTER_BGP, // TODO
    ROUTER_EIGRP_CLASSIC, // TODO
    ROUTER_EIGRP_NAMED, // TODO
    ROUTER_OSPF, // TODO
    ROUTER_OSPFV3, // TODO
    ROUTER_RIP, // TODO
    IPV4_DOMAIN_LIST, // TODO
    IPV4_DOMAIN_LOOKUP_SOURCE_INTERFACE, // TODO
    IPV4_DOMAIN_NAME, // TODO
    IPV4_HOST, // TODO
    IPV4_IGMP_IMMEDIATE_LEAVE_GROUP_LIST, // TODO
    IPV4_IGMP_LIMIT, // TODO
    IPV4_IGMP_SSM_MAP, // TODO
    IPV4_IGMP_SSM_MAP_QUERY_DNS, // TODO
    IPV4_IGMP_SSM_MAP_STATIC, // TODO
    IPV4_MROUTE, // TODO
    IPV4_MSDP, // TODO
    IPV4_MULTICAST, // TODO
    IPV4_MULTICAST_ROUTING, // TODO
    IPV4_NAME_SERVER, // TODO
    IPV4_PIM, // TODO
    IPV4_RADIUS_SOURCE_INTERFACE, // TODO
    IPV4_ROUTE, // TODO
    IPV4_ROUTE_PROFILE, // TODO
    IPV4_ROUTE_STATIC_ADJUST_TIME, // TODO
    IPV4_ROUTE_STATIC_BFD, // TODO
    IPV4_ROUTE_STATIC_INTER_VRF, // TODO
    IPV6_MLD_SSM_MAP, // TODO
    IPV6_MLD_SSM_MAP_QUERY_DNS, // TODO
    IPV6_MLD_SSM_MAP_STATIC, // TODO,
    IPV6_MLD_STATE_LIMIT, //TODO uint16INTER_VRF, // TODO bool
    IPV6_MULTICAST, // TODO
    IPV6_MULTICAST_ROUTING, // TODO bool
    IPV6_PIM, // TODO
    IPV6_ROUTE, // TODO
    IPV6_ROUTE_STATIC_BFD, // TODO
    IPV6_ROUTE_STATIC_RESOLVE, // TODO
    IPV6_ROUTER_EIGRP, // TODO uint16 reference
    IPV6_ROUTER_OSPF, // TODO uint16 reference
    IPV6_ROUTER_RIP_NAME, // TODO string
    IPV6_ROUTER_RIP, // TODO string reference
    COUNT
};

#define VRF_DEFAULTS(X) \
    X(Vrf, IPV4_IGMP_SSM_MAP, false) \
    X(Vrf, IPV4_IGMP_SSM_MAP_QUERY_DNS, false) \
    X(Vrf, IPV4_ROUTE_PROFILE, false) \
    X(Vrf, IPV4_ROUTE_STATIC_ADJUST_TIME, 60) \
    X(Vrf, IPV4_ROUTE_STATIC_INTER_VRF, true) \
    X(Vrf, IPV4_MULTICAST_ROUTING, false) \
    X(Vrf, IPV6_MLD_SSM_MAP, false) \
    X(Vrf, IPV6_MLD_SSM_MAP_QUERY_DNS, false) \
    X(Vrf, IPV6_MULTICAST_ROUTING, false) \
    X(Vrf, IPV6_ROUTE_STATIC_RESOLVE, false)

CONFIG_DEFAULT_TABLE(VRF_DEFAULTS);

#define IP_ROUTE_FIELDS(X) \
    X(types::IPv4Prefix,                 destination) \
    X(interface::InterfaceKey,           outIface) \
    X(std::optional<types::IPv4Address>, forwardAddress) \
    X(std::optional<std::string>,        name) \
    X(uint8_t,                           distance) \
    X(bool,                              dhcp) \
    X(bool,                              multicast) \
    X(bool,                              permanent) \
    X(std::optional<uint32_t>,           tag) \
    X(std::optional<uint16_t>,           track)

DEFINE_TUPLE_SCHEMA(IPRoute, IP_ROUTE_FIELDS)

#define IPV6_ROUTE_FIELDS(X) \
    X(types::IPv6Prefix,                 destination) \
    X(interface::InterfaceKey,           outIface) \
    X(std::optional<types::IPv6Address>, forwardAddress) \
    X(std::string,                       name) \
    X(uint8_t,                           distance) \
    X(bool,                              multicast) \
    X(bool,                              unicast) \
    X(std::string,                       nexthopVrf) \
    X(std::optional<uint32_t>,           tag)

DEFINE_TUPLE_SCHEMA(IPv6Route, IPV6_ROUTE_FIELDS)

using VrfRegistry = SubRegistry<Vrf,
    ValueField<std::vector<std::tuple<types::IPv4Address, types::Mac, std::optional<interface::InterfaceKey>>> CONFIG_INDEX_ARG(ARP_STATIC_ENTRY)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Vrf::ROUTER_BGP_AS)>,
    ReferenceContainer<BgpRegistry CONFIG_INDEX_ARG(Vrf::ROUTER_BGP)>,
    OwnedListField<EigrpRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_EIGRP_CLASSIC)>,
    OwnedListField<EigrpRegistry, std::string CONFIG_INDEX_ARG(Vrf::ROUTER_EIGRP_NAMED)>,
    OwnedListField<OspfRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_OSPF)>,
    OwnedListField<OspfRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_OSPFV3)>,
    ReferenceContainer<EmptyRegistry CONFIG_INDEX_ARG(Vrf::ROUTER_RIP)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV4_DOMAIN_LIST)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(Vrf::IPV4_DOMAIN_LOOKUP_SOURCE_INTERFACE)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV4_DOMAIN_NAME)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV4_HOST)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Vrf::IPV4_IGMP_IMMEDIATE_LEAVE_GROUP_LIST)>,
    OptionalValueField<uint16_t CONFIG_INDEX_ARG(Vrf::IPV4_IGMP_LIMIT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV4_IGMP_SSM_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV4_IGMP_SSM_MAP_QUERY_DNS)>,
    OptionalValueField<std::tuple<std::string, types::IPv4Address> CONFIG_INDEX_ARG(Vrf::IPV4_IGMP_SSM_MAP_STATIC)>,
    ValueField<std::vector<std::tuple<types::IPv4Prefix, interface::InterfaceKey, uint8_t>> CONFIG_INDEX_ARG(Vrf::IPV4_MROUTE)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV4_MSDP)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV4_MULTICAST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV4_MULTICAST_ROUTING)>,
    OptionalAtomicField<types::IPAddress CONFIG_INDEX_ARG(Vrf::IPV4_NAME_SERVER)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV4_PIM)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(Vrf::IPV4_RADIUS_SOURCE_INTERFACE)>,
    ValueField<std::vector<IPRoute> CONFIG_INDEX_ARG(Vrf::IPV4_ROUTE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV4_ROUTE_PROFILE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Vrf::IPV4_ROUTE_STATIC_ADJUST_TIME)>,
    ValueField<std::vector<std::tuple<interface::InterfaceKey, types::IPv4Address, std::optional<std::string>, bool>> CONFIG_INDEX_ARG(Vrf::IPV4_ROUTE_STATIC_BFD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV4_ROUTE_STATIC_INTER_VRF)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_MLD_SSM_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_MLD_SSM_MAP_QUERY_DNS)>,
    ValueField<std::tuple<std::string, types::IPv6Address> CONFIG_INDEX_ARG(Vrf::IPV6_MLD_SSM_MAP_STATIC)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Vrf::IPV6_MLD_STATE_LIMIT)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV6_MULTICAST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_MULTICAST_ROUTING)>,
    OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV6_PIM)>,
    ValueField<std::vector<IPv6Route> CONFIG_INDEX_ARG(Vrf::IPV6_ROUTE)>,
    ValueField<std::vector<std::tuple<interface::InterfaceKey, types::IPv6Address, bool>> CONFIG_INDEX_ARG(Vrf::IPV6_ROUTE_STATIC_BFD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_ROUTE_STATIC_RESOLVE)>,
    OwnedListField<EigrpRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::IPV6_ROUTER_EIGRP)>,
    OwnedListField<OspfRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::IPV6_ROUTER_OSPF)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Vrf::IPV6_ROUTER_RIP_NAME)>,
    ReferenceContainer<EmptyRegistry CONFIG_INDEX_ARG(Vrf::ROUTER_RIP)>
>;
}

#endif // VRF_REGISTRY_HPP
