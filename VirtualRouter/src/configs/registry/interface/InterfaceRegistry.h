/**
 * @file InterfaceRegistry.h
 * @brief Per-interface configuration schema: IP addressing, BFD, QoS, OSPF, EIGRP, and NDP.
 * @ingroup CONFIG_INTERFACE
 */

/**
 * @defgroup CONFIG_INTERFACE Interface Configuration
 * @ingroup CONFIG
 * @brief Configuration schemas for network interface parameters.
 */

#ifndef INTERFACE_REGISTRY_H
#define INTERFACE_REGISTRY_H

#include <IPAddress.h>
#include <Mac.hpp>

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "interface/configs/InterfaceType.hpp"

#include "ArpRegistry.h"
#include "NdpRegistry.h"

#undef IP_MTU
#undef IPV6_MTU

struct IncompleteIf {};
enum class EmptyIf { COUNT };
struct EmptyIfFields : config::FieldTuple<> {};
struct EmptyRegistryIf : public config::SubRegistry<EmptyRegistryIf, EmptyIf, nullptr, EmptyIfFields> {};

namespace interface { struct InterfaceKey; }

namespace config
{

/**
 * @brief Reserved base enum for shared interface field indices (currently unused).
 * @ingroup CONFIG_INTERFACE
 */
enum class InterfaceBase
{
};

void interfaceIPAddress(void*);
void interfaceIPAddressSecondary(void*);
void interfaceShutdown(void*);
void interfaceIPv6Eigrp(void*);

#define INTERFACE_SECONDARY_ADDRESS_FIELDS(X) \
    X(types::IPv4Prefix,  prefix) \
    X(std::string,        vrf)

DEFINE_TUPLE_SCHEMA(InterfaceSecondaryAddress, INTERFACE_SECONDARY_ADDRESS_FIELDS);

#define INTERFACE_HELPER_ADDRESS_FIELDS(X) \
    X(types::IPv4Address, address) \
    X(bool,               global) \
    X(std::string,        vrf)

DEFINE_TUPLE_SCHEMA(InterfaceHelperAddress, INTERFACE_HELPER_ADDRESS_FIELDS);

#define INTERFACE_IPV6_ADDRESS_FIELDS(X) \
    X(types::IPv6Address, address) \
    X(std::string,        prefixName) \
    X(IGNOR(bool),        eui64) \
    X(IGNOR(bool),        anycast)

DEFINE_TUPLE_SCHEMA(InterfaceIPv6Address, INTERFACE_IPV6_ADDRESS_FIELDS);

/**
 * @brief Configuration fields for a single network interface.
 * @ingroup CONFIG_INTERFACE
 *
 * Covers all Cisco IOS-style interface sub-commands: IP addressing, BFD, ARP, NDP,
 * EIGRP/OSPF per-interface parameters, QoS service policies, keepalive, MTU, and
 * shutdown state. Fields marked `// TODO` are schema placeholders not yet fully
 * implemented.
 */
#define INTERFACE_FIELD_LIST(X, Y) \
    VALUE_FIELD(X, Y, AAA_CONNECTION_INFO, std::string) TODO \
    REGISTRY_CONTAINER(X, Y, ARP, ArpRegistry) \
    ATOMIC_FIELD(X, Y, BANDWIDTH, uint32_t, 100000) TODO \
    ATOMIC_FIELD(X, Y, BANDWIDTH_RECEIVE, uint32_t, 0) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BANDWIDTH_INHERITANCE, uint32_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, BANDWIDTH_RECEIVE_INHERITANCE, uint32_t) TODO \
    ATOMIC_FIELD(X, Y, BFD_ECHO, bool, true) TODO \
    ATOMIC_FIELD(X, Y, BFD_INTERVAL, uint16_t, 250) TODO \
    ATOMIC_FIELD(X, Y, BFD_INTERVAL_MIN_RX, uint16_t, 250) TODO \
    ATOMIC_FIELD(X, Y, BFD_INTERVAL_MULTIPLIER, uint8_t, 3) TODO \
    VALUE_FIELD(X, Y, BFD_TEMPLATE, std::string) TODO \
    ATOMIC_FIELD(X, Y, CDP, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, CHANNEL_GROUP, uint8_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, CRYPTO, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, DAMPENING_HALF_LIFE, uint8_t, 5) TODO \
    ATOMIC_FIELD(X, Y, DAMPENING_REUSE, uint16_t, 1000) TODO \
    ATOMIC_FIELD(X, Y, DAMPENING_THRESHOLD, uint16_t, 2000) TODO \
    ATOMIC_FIELD(X, Y, DAMPENING_MAX_DURATION, uint8_t, 20) TODO \
    ATOMIC_FIELD(X, Y, DAMPENING_RESTART_PENALTY, uint16_t, 1000) TODO \
    ATOMIC_FIELD(X, Y, DELAY, uint32_t, 0) TODO \
    VALUE_FIELD(X, Y, DESCRIPTION, std::string) TODO \
    ATOMIC_FIELD(X, Y, DOT1Q_TUNNELING_ETHERNET, uint16_t, 0x8100) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, FLOW_SAMPLER, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, GLBP, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, HISTORY_BPS, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, HISTORY_PPS, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, HOLD_QUEUE_LENGTH, uint32_t, 40) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_ACCESS_GROUP, IncompleteIf) \
    OPTIONAL_ATOMIC_FIELD_CB(X, Y, IP_ADDRESS, types::IPv4Prefix, interfaceIPAddress) TODO \
    LIST_FIELD_CB(X, Y, IP_ADDRESS_SECONDARY, InterfaceSecondaryAddress::Tuple, interfaceIPAddressSecondary) TODO \
    ATOMIC_FIELD_CB(X, Y, IP_ADDRESS_DHCP, bool, false, interfaceIPAddress) TODO \
    ATOMIC_FIELD(X, Y, IP_BFD_FAST_EXTERNAL_FALLOVER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_CEF_ACCOUNTING_NON_RECURSIVE, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_DHCP, IncompleteIf) TODO \
    OWNED_LIST_FIELD(X, Y, IP_EIGRP, EigrpInterfaceRegistry, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_DIRECT_BROADCAST, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_INGRESS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EGRESS, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_FLOW_MONITOR, IncompleteIf) TODO \
    LIST_FIELD(X, Y, IP_HELPER_ADDRESS, InterfaceHelperAddress::Tuple) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_IGMP, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_LISP_SOURCE_LOCATOR, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, IP_LOAD_SHARING_PER_DESTINATION, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_LOAD_SHARING_PER_PACKET, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_LOCAL_PROXY_ARP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_MASK_REPLY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_MFIB_CEF_INPUT, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_MFIB_CEF_OUTPUT, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_MFIB_FORWARDING_INPUT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_MFIB_FORWARDING_OUTPUT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_MTU, uint16_t, 1500) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_MULTICAST, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, IP_NAT_INSIDE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_NAT_OUTSIDE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_NBAR_PROTOCOL_DISCOVER_IP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_NBAR_PROTOCOL_DISCOVER_IPV6, bool, false) TODO \
    REGISTRY_CONTAINER(X, Y, IP_OSPF, OspfGlobalInterfaceRegistry) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_PIM, IncompleteIf) TODO \
    VALUE_FIELD(X, Y, IP_POLICY_ROUTE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, IP_PROXY_ARP, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_REDIRECTS, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_RIP, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, IP_ROUTE_CACHE, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_ROUTE_CACHE_CEF, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_ROUTE_CACHE_FLOW, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ROUTE_CACHE_POLICY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ROUTE_CACHE_SAME_INTERFACE, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_RSVP, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, IP_RTP_COMPRESSION_CONNECTIONS, uint16_t, 16) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_RTP_HEADER_COMPRESSION, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, IP_TCP_ADJUST_MSS, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_TCP_COMPRESSION_CONNECTIONS, uint16_t, 16) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_TCP_HEADER_FORMAT, IncompleteIf) TODO \
    VALUE_FIELD(X, Y, IP_TRAFFIC_EXPORT_APPLY, std::string) TODO \
    ATOMIC_FIELD(X, Y, IP_VERIFY_UNICAST_NOTIFICATION_DROP_RATE, uint32_t, 0) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_VERIFY_UNICAST_REVERSE_PATH, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_VERIFY_UNICAST_REVERSE_PATH_ALLOW_SELF_PING, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_VERIFY_UNICAST_SOURCE_REACHABLE_VIA, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_VRF, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_ADDRESS_LL, types::IPv6Address) TODO \
    LIST_FIELD(X, Y, IPV6_ADDRESS, InterfaceIPv6Address::Tuple) TODO \
    ATOMIC_FIELD(X, Y, IPV6_ADDRESS_AUTOCONFIG, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IPV6_ADDRESS_AUTOCONFIG_DEFAULT, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_ADDRESS_DHCP, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, IPV6_CEF, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_DHCP, IncompleteIf) TODO \
    OWNED_LIST_FIELD(X, Y, IPV6_EIGRP, EigrpInterfaceRegistry, uint16_t) TODO \
    LIST_FIELD_CB(X, Y, IPV6_EIGRP_ENABLED, uint16_t, interfaceIPv6Eigrp) \
    ATOMIC_FIELD(X, Y, IPV6_FLOW_MONITOR, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_LIST_SOURCE_LOCATOR, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MFIB_CEF_INPUT, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MFIB_CEF_OUTPUT, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MFIB_FORWARDING_INPUT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MFIB_FORWARDING_OUTPUT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MFIB_FORWARDING, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MTU, uint16_t, 1500) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_MULTICAST, IncompleteIf) TODO \
    REGISTRY_CONTAINER(X, Y, IPV6_ND, NdpRegistry) \
    REGISTRY_CONTAINER(X, Y, IPV6_OSPF, OspfGlobalInterfaceRegistry) \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_PIM, IncompleteIf) TODO \
    VALUE_FIELD(X, Y, IPV6_POLICY_ROUTE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, IPV6_REDIRECTS, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_RIP, IncompleteIf) TODO \
    VALUE_FIELD(X, Y, IPV6_TRAFFIC_FILTER_IN, std::string) TODO \
    VALUE_FIELD(X, Y, IPV6_TRAFFIC_FILTER_OUT, std::string) TODO \
    ATOMIC_FIELD(X, Y, IPV6_VERIFY_UNICAST_REVERSE_PATH, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_VERIFY_UNICAST_REVERSE_PATH_ACCESS_LIST, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IPV6_VERIFY_UNICAST_SOURCE_REACHABLE_VIA, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, KEEPALIVE, bool, true) TODO \
    ATOMIC_FIELD(X, Y, KEEPALIVE_PERIOD, uint32_t, 10) TODO \
    VALUE_FIELD(X, Y, LAN_NAME, std::string) TODO \
    ATOMIC_FIELD(X, Y, LOAD_INTERVAL, uint16_t, 300) TODO \
    ATOMIC_FIELD(X, Y, LOOPBACK_DRIVER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LOOPBACK_MAC, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, MAC_ADDRESS, types::Mac) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, MPLS, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, MTU, uint16_t, 1500) TODO \
    ATOMIC_FIELD(X, Y, NEGOTIATION_AUTO, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, NTP, IncompleteIf) TODO \
    REGISTRY_CONTAINER(X, Y, OSPFV3_DEFAULT, OspfGlobalInterfaceRegistry) TODO \
    OWNED_LIST_FIELD(X, Y, OSPFV3, OspfInterfaceAfRegistry, uint16_t) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, RATE_LIMIT, IncompleteIf) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, RMON, IncompleteIf) TODO \
    VALUE_FIELD(X, Y, SERVICE_POLICY_INPUT, std::string) TODO \
    VALUE_FIELD(X, Y, SERVICE_POLICY_OUTPUT, std::string) TODO \
    VALUE_FIELD(X, Y, SERVICE_POLICY_CONTROL, std::string) TODO \
    ATOMIC_FIELD(X, Y, SERVICE_POLICY_CONTROL_DEFAULT, bool, false) TODO \
    ATOMIC_FIELD_CB(X, Y, SHUTDOWN, bool, true, interfaceShutdown) TODO \
    ATOMIC_FIELD(X, Y, SNMP_IFINDEX_PERSIST, bool, false) TODO \
    ATOMIC_FIELD(X, Y, SNMP_TRAP_IP_VERIFY_DROP_RATE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, SNMP_TRAP_LINK_STATUS, bool, true) TODO \
    ATOMIC_FIELD(X, Y, SNMP_TRAP_LINK_STATUS_PERMIT_DUPLICATES, bool, false) TODO \
    VALUE_FIELD(X, Y, STANDBY, IncompleteIf) TODO \
    ATOMIC_FIELD(X, Y, TIMEOUT_ABSOLUTE_LIFETIME, uint32_t, 0) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, VLAN_ID_DOT1Q, IncompleteIf) TODO \
    VALUE_FIELD(X, Y, VRF_FORWARDING, std::string) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, VRRP, IncompleteIf) TODO

DEFINE_CONFIG_GROUP(Interface, INTERFACE_FIELD_LIST)

}

#endif // INTERFACE_REGISTRY_H
