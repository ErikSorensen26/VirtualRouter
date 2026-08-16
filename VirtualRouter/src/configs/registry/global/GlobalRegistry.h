/**
 * @file GlobalRegistry.h
 * @brief Global configuration registry: system-wide settings and per-VRF containers.
 * @ingroup CONFIG_GLOBAL
 */

/**
 * @defgroup CONFIG_GLOBAL Global Configuration
 * @ingroup CONFIG
 * @brief Configuration schemas for the global system scope and per-VRF containers.
 */

#ifndef GLOBAL_REGISTRY_HPP
#define GLOBAL_REGISTRY_HPP

#include <IPAddress.h>
#include "configs/RegistryBuilder.hpp"
#include "VrfRegistry.h"
#include "configs/TupleSchema.hpp"
#include "interface/configs/InterfaceType.hpp"

#include "configs/registry/policy/AccessListRegistry.hpp"
#include "configs/registry/policy/RouteMapRegistry.h"
#include "configs/registry/policy/PrefixListRegistry.hpp"

namespace config {
struct InterfaceRegistry;
struct NdpBaseRegistry;
struct EigrpNamedRegistry;
struct BgpRegistry;
struct OspfRegistry;
}

#undef IP_TCP
#undef IP_RSVP

namespace config
{
namespace global
{

/**
 * @brief Naming convention for multilink bundle interfaces.
 * @ingroup CONFIG_GLOBAL
 */
enum class MultilinkBundleName
{
    AUTHENTICATED,
    BOTH,
    ENDPOINT,
    RFC
};

/**
 * @brief IP address pool allocation strategy.
 * @ingroup CONFIG_GLOBAL
 */
enum class AddressPoolMode
{
    DHCP_POOL,
    DHCP_PROXY_CLIENT,
    LOCAL
};

/**
 * @brief Scope of gratuitous ARP announcements sent on interface events.
 * @ingroup CONFIG_GLOBAL
 */
enum class GratuitousType
{
    LOCAL,
    NONE,
    ALL
};

/**
 * @brief IP protocol number aliases used in extended ACL protocol matching.
 * @ingroup CONFIG_GLOBAL
 */
enum class ExtendedAclTypes : uint8_t
{
    IP = 0,
    ICMP = 1,
    IGMP = 2,
    IP_IN_IP = 4,
    TCP = 6,
    UDP = 17,
    GRE = 47,
    ESP = 50,
    AHP = 51,
    EIGRP = 88,
    OSPF = 89,
    NOS = 94,
    PIM = 103,
    PCP = 108
};

/**
 * @brief DSCP codepoint values for QoS classification in ACL matching.
 * @ingroup CONFIG_GLOBAL
 */
enum class Dscp : uint8_t
{
    AF11 = 0b001010,
    AF12 = 0b001100,
    AF13 = 0b001110,
    AF21 = 0b010010,
    AF22 = 0b010100,
    AF23 = 0b010110,
    AF31 = 0b011010,
    AF32 = 0b011100,
    AF33 = 0b011110,
    AF41 = 0b100010,
    AF42 = 0b100100,
    AF43 = 0b100110,
    DSCP_CS1 = 0b001000,
    DSCP_CS2 = 0b010000,
    DSCP_CS3 = 0b011000,
    DSCP_CS4 = 0b100000,
    DSCP_CS5 = 0b101000,
    DSCP_CS6 = 0b110000,
    DSCP_CS7 = 0b111000,
    DF = 000000,
    EF = 0b101110
};

/**
 * @brief IP header option types for extended ACL matching.
 * @ingroup CONFIG_GLOBAL
 */
enum class IPOption : uint8_t
{
};
}

#define IP_EXTENDED_ACL_FIELDS(X) \
    X(uint32_t,    sequence) \
    X(bool,        permit) \
    X(types::IPv4Prefix, source) \
    X(types::IPv4Prefix, destination) \
    X(bool,              log) \
    X(bool,              logInput) \
    X(global::Dscp,      dscp) \
    X(bool,              fragments)

DEFINE_TUPLE_SCHEMA(IPExtendedAcl, IP_EXTENDED_ACL_FIELDS);

#define GLOBAL_IPV6_NEIGHBOR_FIELDS(X) \
    X(types::IPv6Address,       address) \
    X(interface::InterfaceKey,  iface) \
    X(types::Mac,               mac)

DEFINE_TUPLE_SCHEMA(GlobalIPv6Neighbor, GLOBAL_IPV6_NEIGHBOR_FIELDS);

/**
 * @brief Configuration fields for the global system scope.
 * @ingroup CONFIG_GLOBAL
 *
 * This enum indexes every configurable parameter at the global level — from banners
 * and hostname through IP routing, ACLs, prefix-lists, route-maps, and protocol
 * process containers. Fields marked `// TODO` are schema placeholders whose registry
 * types are not yet fully implemented.
 */
// Aliased because VALUE_FIELD() is fixed arity and cannot absorb the type's comma.
using GlobalLineRange = std::pair<uint16_t, uint16_t>;

#define GLOBAL_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, ARCHIVE, bool, false) TODO \
    VALUE_FIELD(X, Y, BANNER, std::string) TODO \
    VALUE_FIELD(X, Y, BANNER_CONFIG_SAVE, std::string) TODO \
    VALUE_FIELD(X, Y, BANNER_EXEC, std::string) TODO \
    VALUE_FIELD(X, Y, BANNER_INCOMING, std::string) TODO \
    VALUE_FIELD(X, Y, BANNER_LOGIN, std::string) TODO \
    VALUE_FIELD(X, Y, BANNER_MOTD, std::string) TODO \
    VALUE_FIELD(X, Y, BANNER_PROMPT_TIMEOUT, std::string) TODO \
    ATOMIC_FIELD(X, Y, BFD_SLOW_TIMERS, uint16_t, 0) TODO \
    OWNED_LIST_FIELD(X, Y, BFD_SINGLE_HOP_TEMPLATES, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, CEF_TABLE, Incomplete) TODO \
    LIST_FIELD(X, Y, CLASS_MAP, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, CLOCK_CALENDAR_VALID, bool, false) TODO \
    LIST_FIELD(X, Y, CLOCK_SUMMER_TIME, Incomplete) TODO \
    LIST_FIELD(X, Y, CLOCK_TIME_ZONE, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, CONFIG_REGISTER, uint16_t, 0x2102) TODO \
    ATOMIC_FIELD(X, Y, CONTROL_PLANE, bool, false) TODO \
    LIST_FIELD(X, Y, CRYPTO, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, EAP_PROFILE, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, ENABLE_PASSWORD, Incomplete) TODO \
    LIST_FIELD(X, Y, ENABLE_SECRET, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, FLOW_EXPORTER, EmptyRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, FLOW_MONITOR, EmptyRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, FLOW_RECORD, EmptyRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, FLOW_SAMPLER_MAP, EmptyRegistry, std::string) TODO \
    VALUE_FIELD(X, Y, HOSTNAME, std::string) TODO \
    OWNED_LIST_FIELD_CB(X, Y, INTERFACE, InterfaceRegistry, interface::InterfaceKey) \
    OWNED_LIST_FIELD(X, Y, IP_ACCESS_LIST_EXTENDED, ExtendedACLRegistry, std::string) \
    ATOMIC_FIELD(X, Y, IP_ACCESS_LIST_HELPER_EGRESS_CHECK, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_ACCESS_LIST_LOGGING_HASH_GENERATION, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ACCESS_LIST_LOGGING_INTERVAL, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC, bool, false) TODO \
    VALUE_FIELD(X, Y, IP_ACCESS_LIST_ROLE_BASED, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, IP_ACCESS_LIST_STANDARD, StandardACLRegistry, std::string) \
    LIST_FIELD(X, Y, IP_ACCOUNTING_LIST, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_ACCOUNTING_THRESHOLD, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ACCOUNTING_TRANSITS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ADDRESS_POOL_DHCP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ADDRESS_POOL_DHCP_PROXY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ADDRESS_POOL_LOCAL, bool, true) TODO \
    ATOMIC_FIELD(X, Y, IP_ARP_GRATUITOUS, int, true) TODO \
    ATOMIC_FIELD(X, Y, IP_ARP_INCOMPLETE, bool, true) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_ARP_INCOMPLETE_ENTRIES, uint32_t) TODO \
    ATOMIC_FIELD(X, Y, IP_ARP_INCOMPLETE_RETRY, uint32_t, 3) TODO \
    ATOMIC_FIELD(X, Y, IP_ARP_PROXY, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ARP_QUEUE, uint32_t, 512) TODO \
    LIST_FIELD(X, Y, IP_AS_PATH_ACCESS_LIST, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_BGP_COMMUNITY_NEW_FORMAT, bool, false) TODO \
    LIST_FIELD(X, Y, IP_CEF, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_CLASSLESS, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_COMMUNITY_LIST, Incomplete) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_DEFAULT_NETWORK, uint32_t) TODO \
    ATOMIC_FIELD(X, Y, IP_DEFAULT_GATEWAY, bool, false) TODO \
    LIST_FIELD(X, Y, IP_DHCP, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_DHCP_CLIENT, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_DHCP_RELAY, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_DHCP_SERVER, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_LOOKUP_NSAP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_LOOKUP_RECURSIVE, bool, false) TODO \
    VALUE_FIELD(X, Y, IP_DOMAIN_MULTICAST, std::string) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_RECURSIVE_ALLOW_SOA, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_RECURSIVE_RETRY, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_RETRY, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_ROUND_ROBIN, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_DOMAIN_TIMEOUT, uint16_t, 0) TODO \
    OWNED_LIST_FIELD(X, Y, IP_EXPLICIT_PATH_IDENTIFIER, EmptyRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, IP_EXPLICIT_PATH_NAME, EmptyRegistry, int) TODO \
    LIST_FIELD(X, Y, IP_EXTCOMMUNITY_LIST, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_FLOW_AGGREGATION_CACHE, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CACHE_ENTRIES, uint32_t, 4096) TODO \
    LIST_FIELD(X, Y, IP_FLOW_CACHE_MPLS, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CACHE_TIMEOUT_ACTIVE, uint8_t, 30) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CACHE_TIMEOUT_INACTIVE, uint8_t, 15) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_FRAGMENT_OFFSET, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_ICMP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_IP_ID, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_MAC, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_PACKET_LENGTH, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_TTL, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_VLAN_ID, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE, bool, false) TODO \
    LIST_FIELD(X, Y, IP_FLOW_EXPORT_DESTINATION, Incomplete) TODO \
    VALUE_FIELD(X, Y, IP_FLOW_EXPORT_SOURCE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_VERSION, uint8_t, 5) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_VERSION_ORIGIN_AS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_EXPORT_VERSION_PEER_AS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_FLOW_TOP_TALKERS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_HOSTNAME_STRICT, bool, false) TODO \
    LIST_FIELD(X, Y, IP_HTTP, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_RATE_LIMIT_UNREACHABLE_DF, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_REDIRECT_HOST, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_ICMP_REDIRECT_SUBNET, bool, false) TODO \
    VALUE_FIELD(X, Y, IP_KERBEROS_SOURCE_INTERFACE, interface::InterfaceKey) TODO \
    VALUE_FIELD(X, Y, IP_LOCAL_POLICY_ROUTE_MAP, std::string) TODO \
    LIST_FIELD(X, Y, IP_LOCAL_POOL, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_MFIB, bool, false) TODO \
    LIST_FIELD(X, Y, IP_NAT, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_NBAR, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_OSPF_NAME_LOOKUP, bool, false) TODO \
    LIST_FIELD(X, Y, IP_POLICY_LIST, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, IP_PREFIX_LIST, PrefixListRegistry<types::IPv4Prefix>, std::string) \
    ATOMIC_FIELD(X, Y, IP_REFLEXIVE_LIST_TIMEOUT, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_ROUTING, bool, true) TODO \
    LIST_FIELD(X, Y, IP_ROUTING_PROTOCOL_PURGE_INTERFACE, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_RSVP, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_SCP_SERVER, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_SECURITY_ESO_INFO_SOURCE, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_SECURITY_ESO_INFO_MAX_C_BYTES, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_SECURITY_ESO_INFO_DEFAULT_BIT, uint8_t, 0) TODO \
    LIST_FIELD(X, Y, IP_SLA, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_SSH, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IP_STICKY_ARP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IP_SUBNET_ZERO, bool, false) TODO \
    VALUE_FIELD(X, Y, IP_TACACS_SOURCE_INTERFACE, interface::InterfaceKey) TODO \
    LIST_FIELD(X, Y, IP_TCP, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_TELNET, Incomplete) TODO \
    LIST_FIELD(X, Y, IP_TFTP, Incomplete) TODO \
    VALUE_FIELD(X, Y, IP_TRAFFIC_EXPORT_PROFILE, std::string) TODO \
    ATOMIC_FIELD(X, Y, IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_VERIFY_DROP_RATE_COMPUTE_WINDOW, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN, uint16_t, 0) TODO \
    OWNED_LIST_FIELD(X, Y, IP_VRF, EmptyRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, IPV6_ACCESS_LIST, ExtendedACLRegistry, std::string) \
    ATOMIC_FIELD(X, Y, IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD, uint32_t, 0) TODO \
    OWNED_LIST_FIELD(X, Y, IPV6_ACCESS_LIST_ROLE_BASED, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, IPV6_CEF, Incomplete) TODO \
    LIST_FIELD(X, Y, IPV6_DHCP, Incomplete) TODO \
    LIST_FIELD(X, Y, IPV6_DHCP_CLIENT, Incomplete) TODO \
    LIST_FIELD(X, Y, IPV6_DHCP_RELAY, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IPV6_FLOWSET, bool, false) TODO \
    LIST_FIELD(X, Y, IPV6_GENERAL_PREFIX, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IPV6_HOP_LIMIT, uint8_t, 64) TODO \
    LIST_FIELD(X, Y, IPV6_HOST, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, IPV6_ICMP_ERROR_INTERVAL, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IPV6_ICMP_BUCKET_SIZE, uint16_t, 0) TODO \
    VALUE_FIELD(X, Y, IPV6_LOCAL_POLICY_ROUTE_MAP, std::string) TODO \
    ATOMIC_FIELD(X, Y, IPV6_MFIB, bool, false) TODO \
    REGISTRY_CONTAINER(X, Y, IPV6_ND, NdpBaseRegistry) TODO \
    LIST_FIELD(X, Y, IPV6_NEIGHBOR, GlobalIPv6Neighbor) TODO \
    ATOMIC_FIELD(X, Y, IPV6_OSPF_NAME_LOOKUP, bool, false) TODO \
    OWNED_LIST_FIELD(X, Y, IPV6_PREFIX_LIST, PrefixListRegistry<types::IPv6Prefix>, std::string) \
    LIST_FIELD(X, Y, IPV6_PREFIX_POOL, Incomplete) TODO \
    VALUE_FIELD(X, Y, IPV6_RADIUS_SOURCE_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, IPV6_SPD_QUEUE_MAX_THRESHOLD, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, IPV6_SPD_QUEUE_MIN_THRESHOLD, uint16_t, 0) TODO \
    VALUE_FIELD(X, Y, IPV6_TACACS_SOURCE_INTERFACE, interface::InterfaceKey) TODO \
    ATOMIC_FIELD(X, Y, IPV6_TRAFFIC_INTERFACE_STATISTICS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE, bool, false) TODO \
    LIST_FIELD(X, Y, KERBEROS, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, KEY_CHAIN, EmptyRegistry, std::string) TODO \
    VALUE_FIELD(X, Y, KEY_CONFIG_KEY, std::string) TODO \
    LIST_FIELD(X, Y, KRON, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, L2_PSEUDOWIRE_ROUTING, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, L2_ROUTER_ID, uint32_t) TODO \
    LIST_FIELD(X, Y, L2_VFI, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, L2VPN_VFI_CONTEXT, Incomplete) TODO \
    LIST_FIELD(X, Y, L2VPN_XCONNECT_CONTEXT, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, L3VPN_ENCAPSULATION_IP_PROFILE, EmptyRegistry, std::string) TODO \
    VALUE_FIELD(X, Y, LINE_RANGE, GlobalLineRange) TODO \
    OWNED_LIST_FIELD(X, Y, LINE_AUX, EmptyRegistry, uint16_t) TODO \
    OWNED_LIST_FIELD(X, Y, LINE_CONSOLE, EmptyRegistry, uint16_t) TODO \
    OWNED_LIST_FIELD(X, Y, LINE_VTY, EmptyRegistry, uint16_t) TODO \
    LIST_FIELD(X, Y, LOGGING, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_BLOCK_FOR_TIME, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_BLOCK_FOR_ATTEMPTS, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_BLOCK_FOR_WITHIN, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_DELAY, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_ON_FAILURE, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_ON_FAILURE_LOG, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_ON_FAILURE_LOG_EVERY, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_ON_SUCCESS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_ON_SUCCESS_LOG, bool, false) TODO \
    ATOMIC_FIELD(X, Y, LOGIN_ON_SUCCESS_LOG_EVERY, uint16_t, 0) TODO \
    VALUE_FIELD(X, Y, LOGIN_QUITE_MODE_ACCESS_CLASS, std::string) TODO \
    VALUE_FIELD(X, Y, LOGIN_STRING_NAME, std::string) TODO \
    VALUE_FIELD(X, Y, LOGIN_STRING_LINE, std::string) TODO \
    ATOMIC_FIELD(X, Y, MLS_RP_IP, bool, false) TODO \
    ATOMIC_FIELD(X, Y, MLS_RP_IP_INPUT_ACL, bool, false) TODO \
    ATOMIC_FIELD(X, Y, MLS_RP_IP_ROUTE_MAP, bool, false) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, MLS_RP_NDE_ADDRESS, uint32_t) TODO \
    LIST_FIELD(X, Y, MONITOR_EVENT_TRACE, Incomplete) TODO \
    LIST_FIELD(X, Y, MPLS, Incomplete) TODO \
    LIST_FIELD(X, Y, NETCONF_FORMAT, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, NETCONF_LOCKTIME, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, NETCONF_MAX_MESSAGE, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, NETCONF_MAX_SESSIONS, uint8_t, 0) TODO \
    LIST_FIELD(X, Y, NTP, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, OBJECT_GROUP_SECURITY, EmptyRegistry, std::string) TODO \
    ATOMIC_FIELD(X, Y, PASSWORD_ENCRYPTION_AES, bool, false) TODO \
    ATOMIC_FIELD(X, Y, PASSWORD_LOGGING, bool, false) TODO \
    LIST_FIELD(X, Y, POLICY_MAP, Incomplete) TODO \
    LIST_FIELD(X, Y, PRIVILEGED, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, QOS_POLICE_ORDER_PARENT_FIRST, bool, false) TODO \
    ATOMIC_FIELD(X, Y, QOS_SHAME_TIMER, int, 0) TODO \
    OWNED_LIST_FIELD(X, Y, ROUTE_MAP, RouteMapRegistry, std::string) \
    OWNED_LIST_FIELD(X, Y, ROUTE_TAG_LIST, EmptyRegistry, std::string) TODO \
    ATOMIC_FIELD(X, Y, ROUTE_TAG_NOTATION_DOTTED_DECIMAL, bool, false) TODO \
    OWNED_LIST_FIELD_CB_VA(X, Y, ROUTER_BGP, BgpRegistry, uint32_t) \
    OWNED_LIST_FIELD_CB(X, Y, ROUTER_EIGRP, EigrpNamedRegistry, uint16_t) \
    OWNED_LIST_FIELD_CB(X, Y, ROUTER_OSPFV3, OspfRegistry, uint16_t) \
    OWNED_LIST_FIELD(X, Y, SAMPLER, EmptyRegistry, std::string) TODO \
    OWNED_LIST_FIELD(X, Y, SASL_PROFILE, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, SCRIPTING_TCL_ENCDIR, Incomplete) TODO \
    LIST_FIELD(X, Y, SCRIPTING_TCL_INIT, Incomplete) TODO \
    OPTIONAL_ATOMIC_FIELD(X, Y, SCRIPTING_TCL_LOW_MEMORY, uint32_t) TODO \
    ATOMIC_FIELD(X, Y, SECURITY_AUTH_FAILURE_RATE_THRESHOLD, uint16_t, 0) TODO \
    ATOMIC_FIELD(X, Y, SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG, bool, false) TODO \
    ATOMIC_FIELD(X, Y, SECURITY_PASSWORDS_MIN_LENGTH, uint8_t, 0) TODO \
    LIST_FIELD(X, Y, SERVICE, Incomplete) TODO \
    VALUE_FIELD(X, Y, SERVICE_POLICY_TYPE_CONTROL, std::string) TODO \
    ATOMIC_FIELD(X, Y, SNMP_IFMIB_IFALIAS_LONG, bool, false) TODO \
    ATOMIC_FIELD(X, Y, SNMP_IFMIB_IFINDEX_PERSIST, bool, false) TODO \
    ATOMIC_FIELD(X, Y, SNMP_IFMIB_TRAP_THROTTLE, bool, false) TODO \
    LIST_FIELD(X, Y, SNMP_MIB, Incomplete) TODO \
    LIST_FIELD(X, Y, SNMP_SERVER, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, STANDBY_BFD_ALL_INTERFACES, bool, false) TODO \
    ATOMIC_FIELD(X, Y, STANDBY_REDIRECTS, bool, false) TODO \
    LIST_FIELD(X, Y, TACACS_SERVER, Incomplete) TODO \
    OWNED_LIST_FIELD(X, Y, TIME_RANGE, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, TRACK_OBJECT, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, TRACK_RESOLUTION_IP_ROUTE_BGP, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, TRACK_RESOLUTION_IP_ROUTE_EIGRP, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, TRACK_RESOLUTION_IP_ROUTE_OSPF, uint32_t, 0) TODO \
    ATOMIC_FIELD(X, Y, TRACK_RESOLUTION_IP_ROUTE_STATIC, uint32_t, 0) TODO \
    LIST_FIELD(X, Y, USERNAME, Incomplete) TODO \
    OWNED_LIST_FIELD_CB(X, Y, VRF_CONFIGS, VrfRegistry, std::string) \
    OWNED_LIST_FIELD(X, Y, VRF_LIST, EmptyRegistry, std::string) TODO \
    LIST_FIELD(X, Y, VRF_SELECTION, Incomplete) TODO \
    ATOMIC_FIELD(X, Y, WARM_REBOOT, bool, false) TODO \
    ATOMIC_FIELD(X, Y, WARM_REBOOT_COUNT, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, WARM_REBOOT_UPTIME, uint8_t, 0) TODO \
    ATOMIC_FIELD(X, Y, XCONNECT_LOGGING_PSEUDOWIRE_STATUS, bool, false) TODO \
    ATOMIC_FIELD(X, Y, XCONNECT_LOGGING_REDUNDANCY, bool, false) TODO

DEFINE_CONFIG_GROUP(Global, GLOBAL_FIELD_LIST)
}

#endif // GLOBAL_REGISTRY_HPP
