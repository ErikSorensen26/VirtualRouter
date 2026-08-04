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
#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"

#include "core/routing/rib/RouteSource.hpp"
#include "interface/configs/InterfaceType.hpp"

namespace config {
struct BgpRegistry;
struct EigrpRegistry;
struct OspfRegistry;
struct Ospfv3AddressFamilyRegistry;
}

#undef IP_PIM

struct Incomplete {};
enum class Empty { COUNT };
class EmptyFields : public config::FieldTuple<> {};
class EmptyRegistry : public config::SubRegistry<EmptyRegistry, Empty, nullptr, EmptyFields> {};

namespace config
{
#define STATIC_ARP_ENTRY_FIELDS(X) \
    X(types::IPv4Address, address) \
    X(IGNOR(types::Mac), mac) \
    X(std::optional<interface::InterfaceKey>, key)

DEFINE_TUPLE_SCHEMA(StaticArpEntry, STATIC_ARP_ENTRY_FIELDS);

#define IP_IGMP_SSM_MAP_STATIC_FIELDS(X) \
    X(std::string, map) \
    X(types::IPv4Address, address)

DEFINE_TUPLE_SCHEMA(IPIgmpSsmMapStatic, IP_IGMP_SSM_MAP_STATIC_FIELDS);

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

DEFINE_TUPLE_SCHEMA(IPRoute, IP_ROUTE_FIELDS);

#define IP_ROUTE_STATIC_BFD_FIELDS(X) \
    X(interface::InterfaceKey, outIface) \
    X(types::IPv4Address, destination) \
    X(std::optional<std::string>, name) \
    X(std::optional<std::string>, multicast)

DEFINE_TUPLE_SCHEMA(IPRouteStaticBfd, IP_ROUTE_STATIC_BFD_FIELDS);

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

DEFINE_TUPLE_SCHEMA(IPv6Route, IPV6_ROUTE_FIELDS);

#define IP_MROUTE_FIELDS(X) \
    X(types::IPv4Prefix,                destination) \
    X(types::IPv4Address,               nextHop) \
    X(interface::InterfaceKey,          iface) \
    X(std::optional<core::RouteSource>, source) \
    X(std::optional<uint32_t>,          sourceId) \
    X(std::optional<uint8_t>,           distance)

DEFINE_TUPLE_SCHEMA(IPMRoute, IP_MROUTE_FIELDS);

#define IPV6_MLD_SSM_MAP_STATIC_FIELDS(X) \
    X(std::string,        map) \
    X(types::IPv6Address, address)

DEFINE_TUPLE_SCHEMA(IPv6MldSsmMapStatic, IPV6_MLD_SSM_MAP_STATIC_FIELDS);

#define IPV6_ROUTE_STATIC_BFD_FIELDS(X) \
    X(interface::InterfaceKey, iface) \
    X(types::IPv6Address,      address) \
    X(bool,                    multicast)

DEFINE_TUPLE_SCHEMA(IPv6RouteStaticBfd, IPV6_ROUTE_STATIC_BFD_FIELDS);

void VrfRouterEigrpV4(void*);
void VrfRouterEigrpV6(void*);

#define VRF_FIELD_LIST(X, Y) \
    LIST_FIELD(X, Y, ARP_STATIC_ENTRY, StaticArpEntry::Tuple) \
    OPTIONAL_REGISTRY_CONTAINER(X, Y,       ROUTER_BGP, BgpRegistry) TODO \
    OWNED_LIST_FIELD_CB(X, Y,               ROUTER_EIGRP_V4, EigrpRegistry, uint16_t, VrfRouterEigrpV4) \
    OWNED_LIST_FIELD_CB(X, Y,               ROUTER_EIGRP_V6, EigrpRegistry, uint16_t, VrfRouterEigrpV6) \
    OWNED_LIST_FIELD(X, Y,                  ROUTER_OSPF, OspfRegistry, uint16_t) TODO \
    OWNED_LIST_FIELD(X, Y,                  ROUTER_OSPFV3, Ospfv3AddressFamilyRegistry, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_DOMAIN_LIST, Incomplete) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_DOMAIN_LOOKUP_SOURCE_INTERFACE, interface::InterfaceKey) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_DOMAIN_NAME, Incomplete) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_HOST, Incomplete) TODO \
    VALUE_FIELD(X, Y,                       IP_IGMP_IMMEDIATE_LEAVE_GROUP_LIST, std::string) TODO \
    VALUE_FIELD(X, Y,                       IP_IGMP_LIMIT, uint16_t) TODO \
    ATOMIC_FIELD(X, Y,                      IP_IGMP_SSM_MAP, bool, false) TODO \
    ATOMIC_FIELD(X, Y,                      IP_IGMP_SSM_MAP_QUERY_DNS, bool, false) TODO \
    VALUE_FIELD(X, Y,                       IP_IGMP_SSM_MAP_STATIC, IPIgmpSsmMapStatic::Tuple) TODO \
    LIST_FIELD(X, Y,                        IP_MROUTE, IPMRoute::Tuple) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_MSDP, Incomplete) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_MULTICAST, Incomplete) TODO \
    ATOMIC_FIELD(X, Y,                      IP_MULTICAST_ROUTING, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_NAME_SERVER, types::IPAddress) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_PIM, Incomplete) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IP_RADIUS_SOURCE_INTERFACE, interface::InterfaceKey) TODO \
    LIST_FIELD(X, Y,                        IP_ROUTE, IPRoute::Tuple) TODO \
    ATOMIC_FIELD(X, Y,                      IP_ROUTE_PROFILE, bool, false) TODO \
    ATOMIC_FIELD(X, Y,                      IP_ROUTE_STATIC_ADJUST_TIME, uint8_t, 60) TODO \
    LIST_FIELD(X, Y,                        IP_ROUTE_STATIC_BFD, IPRouteStaticBfd::Tuple) TODO \
    ATOMIC_FIELD(X, Y,                      IP_ROUTE_STATIC_INTER_VRF, bool, true) TODO \
    ATOMIC_FIELD(X, Y,                      IPV6_MLD_SSM_MAP, bool, false) TODO \
    ATOMIC_FIELD(X, Y,                      IPV6_MLD_SSM_MAP_QUERY_DNS, bool, false) TODO \
    LIST_FIELD(X, Y,                        IPV6_MLD_SSM_MAP_STATIC, IPv6MldSsmMapStatic::Tuple) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IPV6_MLD_STATE_LIMIT, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IPV6_MULTICAST, Incomplete) TODO \
    ATOMIC_FIELD(X, Y,                      IPV6_MULTICAST_ROUTING, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y,             IPV6_PIM, Incomplete) TODO \
    LIST_FIELD(X, Y,                        IPV6_ROUTE, IPv6Route::Tuple) TODO \
    LIST_FIELD(X, Y,                        IPV6_ROUTE_STATIC_BFD, IPv6RouteStaticBfd::Tuple) TODO \
    ATOMIC_FIELD(X, Y,                      IPV6_ROUTE_STATIC_RESOLVE, bool, false) TODO \
    OWNED_LIST_FIELD(X, Y,                  IPV6_ROUTER_OSPF, OspfRegistry, uint16_t) TODO

DEFINE_CONFIG_GROUP(Vrf, VRF_FIELD_LIST)

TUPLE_SCHEMA_FOR(Vrf, Vrf::ARP_STATIC_ENTRY, StaticArpEntry);
}

#endif // VRF_REGISTRY_HPP
