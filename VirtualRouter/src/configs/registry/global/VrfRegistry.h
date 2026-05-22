/**
 * @file VrfRegistry.h
 * @brief Per-VRF configuration registry: routing, multicast, and static route settings.
 * @ingroup CONFIG_GLOBAL
 */

#ifndef VRF_REGISTRY_HPP
#define VRF_REGISTRY_HPP

#include <IPAddress.h>
#include <Mac.hpp>

#include "configs/RegistryTypes.hpp"
#include "configs/SubRegistry.hpp"

#include "core/routing/rib/RouteSource.hpp"
#include "configs/registry/router/BgpRegistry.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/registry/router/OspfRegistry.h"

#undef IP_PIM

struct Incomplete {};
enum class Empty { COUNT };
using EmptyRegistry = config::SubRegistry<Empty, nullptr>;

namespace config
{
/**
 * @brief Configuration fields for a single VRF instance.
 * @ingroup CONFIG_GLOBAL
 *
 * Covers static routes (IPv4 and IPv6), multicast settings, BFD tracking, EIGRP and
 * OSPF protocol process containers, and ARP/NDP static entries. Fields marked `// TODO`
 * are schema placeholders not yet fully implemented.
 */
enum class Vrf
{
    ARP_STATIC_ENTRY,
    ROUTER_BGP_AS, // TODO
    ROUTER_BGP, // TODO
    ROUTER_EIGRP_V4,
    ROUTER_EIGRP_V6,
    ROUTER_OSPF, // TODO
    ROUTER_OSPFV3, // TODO
    IP_DOMAIN_LIST, // TODO
    IP_DOMAIN_LOOKUP_SOURCE_INTERFACE, // TODO
    IP_DOMAIN_NAME, // TODO
    IP_HOST, // TODO
    IP_IGMP_IMMEDIATE_LEAVE_GROUP_LIST, // TODO
    IP_IGMP_LIMIT, // TODO
    IP_IGMP_SSM_MAP, // TODO
    IP_IGMP_SSM_MAP_QUERY_DNS, // TODO
    IP_IGMP_SSM_MAP_STATIC, // TODO
    IP_MROUTE, // TODO
    IP_MSDP, // TODO
    IP_MULTICAST, // TODO
    IP_MULTICAST_ROUTING, // TODO
    IP_NAME_SERVER, // TODO
    IP_PIM, // TODO
    IP_RADIUS_SOURCE_INTERFACE, // TODO
    IP_ROUTE, // TODO
    IP_ROUTE_PROFILE, // TODO
    IP_ROUTE_STATIC_ADJUST_TIME, // TODO
    IP_ROUTE_STATIC_BFD, // TODO
    IP_ROUTE_STATIC_INTER_VRF, // TODO
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
    IPV6_ROUTER_OSPF, // TODO uint16 reference
    COUNT
};

#define VRF_DEFAULTS(X) \
    X(Vrf, IP_IGMP_SSM_MAP, false) \
    X(Vrf, IP_IGMP_SSM_MAP_QUERY_DNS, false) \
    X(Vrf, IP_ROUTE_PROFILE, false) \
    X(Vrf, IP_ROUTE_STATIC_ADJUST_TIME, 60) \
    X(Vrf, IP_ROUTE_STATIC_INTER_VRF, true) \
    X(Vrf, IP_MULTICAST_ROUTING, false) \
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
    X(std::optional<uint8_t>,            distance) \
    X(IGNOR(bool),                       dhcp) \
    X(IGNOR(bool),                       multicast) \
    X(IGNOR(bool),                       permanent) \
    X(std::optional<uint32_t>,           tag) \
    X(std::optional<uint16_t>,           track)

DEFINE_TUPLE_SCHEMA(IPRoute, IP_ROUTE_FIELDS)

#define IPV6_ROUTE_FIELDS(X) \
    X(types::IPv6Prefix,                 destination) \
    X(interface::InterfaceKey,           outIface) \
    X(std::optional<types::IPv6Address>, forwardAddress) \
    X(std::optional<std::string>,        name) \
    X(std::optional<uint8_t>,            distance) \
    X(IGNOR(bool),                       multicast) \
    X(IGNOR(bool),                       unicast) \
    X(std::optional<std::string>,        nexthopVrf) \
    X(std::optional<uint32_t>,           tag)

DEFINE_TUPLE_SCHEMA(IPv6Route, IPV6_ROUTE_FIELDS)

#define IP_MROUTE_FIELDS(X) \
    X(types::IPv4Prefix,                destination) \
    X(types::IPv4Address,               nextHop) \
    X(interface::InterfaceKey,          iface) \
    X(std::optional<core::RouteSource>, source) \
    X(std::optional<uint32_t>,          sourceId) \
    X(std::optional<uint8_t>,           distance)

DEFINE_TUPLE_SCHEMA(IPMRoute, IP_MROUTE_FIELDS)

void VrfRouterEigrpV4(void*);
void VrfRouterEigrpV6(void*);

/**
 * @brief Registry slot for one VRF instance.
 * @ingroup CONFIG_GLOBAL
 *
 * Owns a `SubRegistry<Vrf, ...>` containing all per-VRF fields. Multiple VrfRegistry
 * instances are stored in `GlobalRegistry::VRF_CONFIGS` keyed by VRF name.
 */
struct VrfRegistry
{
    SubRegistry<Vrf, nullptr,
        ListField<std::tuple<types::IPv4Address, IGNOR(types::Mac), std::optional<interface::InterfaceKey>> CONFIG_INDEX_ARG(Vrf::ARP_STATIC_ENTRY)>,
        OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Vrf::ROUTER_BGP_AS)>,
        RegistryContainer<BgpRegistry CONFIG_INDEX_ARG(Vrf::ROUTER_BGP)>,
        OwnedListField<EigrpRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_EIGRP_V4), VrfRouterEigrpV4>,
        OwnedListField<EigrpRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_EIGRP_V6), VrfRouterEigrpV6>,
        OwnedListField<OspfRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_OSPF)>,
        OwnedListField<Ospfv3AddressFamilyRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::ROUTER_OSPFV3)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IP_DOMAIN_LIST)>,
        OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(Vrf::IP_DOMAIN_LOOKUP_SOURCE_INTERFACE)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IP_DOMAIN_NAME)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IP_HOST)>,
        ValueField<std::string CONFIG_INDEX_ARG(Vrf::IP_IGMP_IMMEDIATE_LEAVE_GROUP_LIST)>,
        ValueField<uint16_t CONFIG_INDEX_ARG(Vrf::IP_IGMP_LIMIT)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IP_IGMP_SSM_MAP)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IP_IGMP_SSM_MAP_QUERY_DNS)>,
        ValueField<std::tuple<std::string, types::IPv4Address> CONFIG_INDEX_ARG(Vrf::IP_IGMP_SSM_MAP_STATIC)>,
        ListField<IPMRoute CONFIG_INDEX_ARG(Vrf::IP_MROUTE)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IP_MSDP)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IP_MULTICAST)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IP_MULTICAST_ROUTING)>,
        OptionalAtomicField<types::IPAddress CONFIG_INDEX_ARG(Vrf::IP_NAME_SERVER)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IP_PIM)>,
        OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(Vrf::IP_RADIUS_SOURCE_INTERFACE)>,
        ListField<IPRoute CONFIG_INDEX_ARG(Vrf::IP_ROUTE)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IP_ROUTE_PROFILE)>,
        AtomicField<uint8_t CONFIG_INDEX_ARG(Vrf::IP_ROUTE_STATIC_ADJUST_TIME)>,
        ListField<std::tuple<interface::InterfaceKey, types::IPv4Address, std::optional<std::string>, bool> CONFIG_INDEX_ARG(Vrf::IP_ROUTE_STATIC_BFD)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IP_ROUTE_STATIC_INTER_VRF)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_MLD_SSM_MAP)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_MLD_SSM_MAP_QUERY_DNS)>,
        ListField<std::tuple<std::string, types::IPv6Address> CONFIG_INDEX_ARG(Vrf::IPV6_MLD_SSM_MAP_STATIC)>,
        OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Vrf::IPV6_MLD_STATE_LIMIT)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV6_MULTICAST)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_MULTICAST_ROUTING)>,
        OptionalAtomicField<Incomplete CONFIG_INDEX_ARG(Vrf::IPV6_PIM)>,
        ListField<IPv6Route CONFIG_INDEX_ARG(Vrf::IPV6_ROUTE)>,
        ListField<std::tuple<interface::InterfaceKey, types::IPv6Address, bool> CONFIG_INDEX_ARG(Vrf::IPV6_ROUTE_STATIC_BFD)>,
        AtomicField<bool CONFIG_INDEX_ARG(Vrf::IPV6_ROUTE_STATIC_RESOLVE)>,
        OwnedListField<OspfRegistry, uint16_t CONFIG_INDEX_ARG(Vrf::IPV6_ROUTER_OSPF)>
    > reg;
};
}

#endif // VRF_REGISTRY_HPP
