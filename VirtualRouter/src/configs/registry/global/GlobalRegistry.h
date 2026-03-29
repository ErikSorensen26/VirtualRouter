/**
 * @file GlobalRegistry.h
 * @brief Global configuration registry
 *
 * Defines the configuration schema for the global scope.
 */

#ifndef GLOBAL_REGISTRY_HPP
#define GLOBAL_REGISTRY_HPP

#include <vector>
#include <IPAddress.h>
#include "configs/RegistryTypes.hpp"
#include "VrfRegistry.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "configs/TupleSchema.hpp"
#include "interface/configs/InterfaceType.hpp"

struct Incomplete {};
enum Empty { COUNT };
using EmptyRegistry = config::SubRegistry<Empty>;

namespace config
{
namespace global
{

enum class MultilinkBundleName
{
    AUTHENTICATED,
    BOTH,
    ENDPOINT,
    RFC
};

enum class AddressPoolMode
{
    DHCP_POOL,
    DHCP_PROXY_CLIENT,
    LOCAL
};

enum class GratuitousType
{
    LOCAL,
    NONE,
    ALL
};

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
    CS1 = 0b001000,
    CS2 = 0b010000,
    CS3 = 0b011000,
    CS4 = 0b100000,
    CS5 = 0b101000,
    CS6 = 0b110000,
    CS7 = 0b111000,
    DEFAULT = 000000,
    EF = 0b101110
};

enum class IPOption : uint8_t
{

};
}

enum class Global2
{
    ARCHIVE, // TODO bool
    BANNER, // TODO string
    BANNER_CONFIG_SAVE, // TODO string
    BANNER_EXEC, // TODO string
    BANNER_INCOMING, // TODO string
    BANNER_LOGIN, // TODO string
    BANNER_MOTD, // TODO string
    BANNER_PROMPT_TIMEOUT, // TODO string
    BFD_SLOW_TIMERS, // TODO uint16_t
    BFD_SINGLE_HOP_TEMPLATES, // TODO string reference
    CEF_TABLE, // TODO
    CLASS_MAP, // TODO
    CLOCK_CALENDAR_VALID, // TODO bool
    CLOCK_SUMMER_TIME, // TODO
    CLOCK_TIME_ZONE, // TODO
    CONFIG_REGISTER, // TODO uint16_t
    CONTROL_PLANE, // TODO bool
    CRYPTO, // TODO
    EAP_PROFILE, // TODO string reference
    ENABLE_PASSWORD, // TODO
    ENABLE_SECRET, // TODO
    FLOW_EXPORTER, // TODO string reference
    FLOW_MONITOR, // TODO string reference
    FLOW_RECORD, // TODO string reference
    FLOW_SAMPLER_MAP, // TODO string reference
    HOSTNAME, // TODO string
    INTERFACE, // TODO interfacekey reference
    IP_ACCESS_LIST_EXTENDED, // TODO
    IP_ACCESS_LIST_HELPER_EGRESS_CHECK, // TODO bool
    IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD, // TODO uint32
    IP_ACCESS_LIST_LOGGING_HASH_GENERATION, // TODO bool
    IP_ACCESS_LIST_LOGGING_INTERVAL, // TODO uint32
    IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC, // TODO bool
    IP_ACCESS_LIST_ROLE_BASED, // TODO
    IP_ACCESS_LIST_STANDARD, // TODO
    IP_ACCOUNTING_LIST, // TODO
    IP_ACCOUNTING_THRESHOLD, // TODO bool
    IP_ACCOUNTING_TRANSITS, // TODO bool
    IP_ADDRESS_POOL, // TODO AddressPoolMode
    IP_ARP_GRATUITOUS, // TODO GratuitousType
    IP_ARP_INCOMPLETE, // TODO bool
    IP_ARP_INCOMPLETE_ENTRIES, // TODO uint32
    IP_ARP_INCOMPLETE_RETRY, // TODO uitn32
    IP_ARP_PROXY, // TODO bool
    IP_ARP_QUEUE, // TODO uint32
    IP_AS_PATH_ACCESS_LIST, // TODO
    IP_BGP_COMMUNITY_NEW_FORMAT, // TODO bool
    IP_CEF, // TODO
    IP_CLASSLESS, // TODO
    IP_COMMUNITY_LIST, // TODO
    IP_DEFAULT_NETWORK, // TODO ipv4
    IP_DEFAULT_GATEWAY, // TODO bool
    IP_DHCP, // TODO
    IP_DHCP_CLIENT, // TODO
    IP_DHCP_RELAY, // TODO
    IP_DHCP_SERVER, // TODO
    IP_DOMAIN_LOOKUP_NSAP, // TODO bool
    IP_DOMAIN_LOOKUP_RECURSIVE, // TODO bool
    IP_DOMAIN_MULTICAST, // TODO string
    IP_DOMAIN_RECURSIVE_ALLOW_SOA, // TODO bool
    IP_DOMAIN_RECURSIVE_RETRY, // TODO uint8
    IP_DOMAIN_RETRY, // TODO uint8
    IP_DOMAIN_ROUND_ROBIN, // TODO bool
    IP_DOMAIN_TIMEOUT, // TODO uint16
    IP_EXPLICIT_PATH_IDENTIFIER, // TODO string reference
    IP_EXPLICIT_PATH_NAME, // TODO int reference
    IP_EXTCOMMUNITY_LIST, // TODO
    IP_FLOW_AGGREGATION_CACHE, // TODO
    IP_FLOW_CACHE_ENTRIES, // TODO uint32
    IP_FLOW_CACHE_MPLS, // TODO
    IP_FLOW_CACHE_TIMEOUT_ACTIVE, // TODO uint8
    IP_FLOW_CACHE_TIMEOUT_INACTIVE, // TODO uint8
    IP_FLOW_CAPTURE_FRAGMENT_OFFSET, // TODO bool
    IP_FLOW_CAPTURE_ICMP, // TODO bool
    IP_FLOW_CAPTURE_IP_ID, // TODO bool
    IP_FLOW_CAPTURE_MAC, // TODO bool
    IP_FLOW_CAPTURE_PACKET_LENGTH, // TODO bool
    IP_FLOW_CAPTURE_TTL, // TODO bool
    IP_FLOW_CAPTURE_VLAN_ID, // TODO bool
    IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE, // TODO bool
    IP_FLOW_EXPORT_DESTINATION, // TODO
    IP_FLOW_EXPORT_SOURCE, // TODO interfacekey
    IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS, // TODO bool
    IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE, // TODO uint16
    IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE, // TODO uint16
    IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE, // TODO uint16
    IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE, // TODO uint16
    IP_FLOW_EXPORT_VERSION, // TODO uint8 1/5/9
    IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP, // TODO bool
    IP_FLOW_EXPORT_VERSION_ORIGIN_AS, // TODO bool
    IP_FLOW_EXPORT_VERSION_PEER_AS, // TODO bool
    IP_FLOW_TOP_TALKERS, // TODO bool
    IP_HOSTNAME_STRICT, // TODO bool
    IP_HTTP, // TODO
    IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS, // TODO uint32
    IP_ICMP_RATE_LIMIT_UNREACHABLE_DF, // TODO bool
    IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG, // TODO bool
    IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER, // TODO uint32
    IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS, // TODO uint32
    IP_ICMP_REDIRECT_HOST, // TODO bool
    IP_ICMP_REDIRECT_SUBNET, // TODO bool
    IP_KERBEROS_SOURCE_INTERFACE, // TODO interfacekey
    IP_LOCAL_POLICY_ROUTE_MAP, // TODO string
    IP_LOCAL_POOL, // TODO
    IP_MFIB, // TODO bool
    IP_NAT, // TODO
    IP_NBAR, // TODO
    IP_OSPF_NAME_LOOKUP, // TODO bool
    IP_POLICY_LIST, // TODO
    IP_PREFIX_LIST, // TODO
    IP_REFLEXIVE_LIST_TIMEOUT, // TODO uint32
    IP_ROUTING, // TODO bool
    IP_ROUTING_PROTOCOL_PURGE_INTERFACE, // TODO, 
    IP_RSVP, // TODO
    IP_SCP_SERVER, // TODO bool
    IP_SECURITY_ESO_INFO_SOURCE, // TODO uint8
    IP_SECURITY_ESO_INFO_MAX_C_BYTES, // TODO uint8
    IP_SECURITY_ESO_INFO_DEFAULT_BIT, // TODO uint8
    IP_SLA, // TODO
    IP_SSH, // TODO
    IP_STICKY_ARP, // TODO bool
    IP_SUBNET_ZERO, // TODO bool
    IP_TACACS_SOURCE_INTERFACE, // TODO interfacekey
    IP_TCP, // TODO
    IP_TELNET, // TODO
    IP_TFTP, // TODO
    IP_TRAFFIC_EXPORT_PROFILE, // TODO string
    IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL, // TODO uint16
    IP_VERIFY_DROP_RATE_COMPUTE_WINDOW, // TODO uint16
    IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN, // TODO uint16
    IP_VRF, // TODO string reference
    IPV6_ACCESS_LIST, // TODO word reference
    IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD, // TODO uint32
    IPV6_ACCESS_LIST_ROLE_BASED, // TODO word reference
    IPV6_CEF, // TODO
    IPV6_DHCP, // TODO
    IPV6_DHCP_CLIENT, // TODO
    IPV6_DHCP_RELAY, // TODO
    IPV6_FLOWSET, // TODO bool
    IPV6_GENERAL_PREFIX, // TODO
    IPV6_HOP_LIMIT, // TODO uint8
    IPV6_HOST, // TODO
    IPV6_ICMP_ERROR_INTERVAL, // TODO uint32
    IPV6_ICMP_BUCKET_SIZE, //TODO uint16
    IPV6_LOCAL_POLICY_ROUTE_MAP, // TODO string
    IPV6_MFIB, // TODO bool
    IPV6_ND, // TODO reference
    IPV6_NEIGHBOR, // TODO
    IPV6_OSPF_NAME_LOOKUP, // TODO bool
    IPV6_PREFIX_LIST, // TODO
    IPV6_PREFIX_POOL, // TODO
    IPV6_RADIUS_SOURCE_INTERFACE, // TODO interfacekey
    IPV6_SPD_QUEUE_MAX_THRESHOLD, // TODO uint16
    IPV6_SPD_QUEUE_MIN_THRESHOLD, // TODO uint16
    IPV6_TACACS_SOURCE_INTERFACE, // TODO interfacekey
    IPV6_TRAFFIC_INTERFACE_STATISTICS, // TODO bool
    IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE, // TODO bool
    KERBEROS, // TODO
    KEY_CHAIN, // TODO string reference
    KEY_CONFIG_KEY, // TODO string
    KRON, // TODO
    L2_PSEUDOWIRE_ROUTING, // TODO bool
    L2_ROUTER_ID, // TODO ipv4
    L2_VFI, // TODO
    L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS, // TODO string reference
    L2VPN_VFI_CONTEXT, // TODO
    L2VPN_XCONNECT_CONTEXT, // TODO
    L3VPN_ENCAPSULATION_IP_PROFILE, // TODO string reference
    LINE_RANGE, // TODO pair<uint16, uint16>
    LINE_AUX, // TODO uint16 reference
    LINE_CONSOLE, // TODO uint16 reference
    LINE_VTY, // TODO uint16 reference
    LOGGING, // TODO 
    LOGIN_BLOCK_FOR_TIME, // TODO uint16
    LOGIN_BLOCK_FOR_ATTEMPTS, // TODO uint16
    LOGIN_BLOCK_FOR_WITHIN, // TODO uint16
    LOGIN_DELAY, // TODO uint8
    LOGIN_ON_FAILURE, // TODO bool
    LOGIN_ON_FAILURE_LOG, // TODO bool
    LOGIN_ON_FAILURE_LOG_EVERY, // TODO uint16
    LOGIN_ON_SUCCESS, // TODO bool
    LOGIN_ON_SUCCESS_LOG, // TODO bool
    LOGIN_ON_SUCCESS_LOG_EVERY, // TODO uint16
    LOGIN_QUITE_MODE_ACCESS_CLASS, // TODO string
    LOGIN_STRING_NAME, // TODO string
    LOGIN_STRING_LINE, // TODO string
    MLS_RP_IP, // TODO bool
    MLS_RP_IP_INPUT_ACL, // TODO bool
    MLS_RP_IP_ROUTE_MAP, // TODO bool
    MLS_RP_NDE_ADDRESS, // TODO ipv4
    MONITOR_EVENT_TRACE, // TODO
    MPLS, // TODO
    NETCONF_FORMAT, // TODO
    NETCONF_LOCKTIME, // TODO uint16
    NETCONF_MAX_MESSAGE, // TODO uint32
    NETCONF_MAX_SESSIONS, // TODO uint8
    NTP, // TODO
    OBJECT_GROUP_SECURITY, // TODO string reference
    PASSWORD_ENCRYPTION_AES, // TODO bool
    PASSWORD_LOGGING, // TODO bool
    POLICY_MAP, // TODO 
    PRIVILEGED, // TODO
    QOS_POLICE_ORDER_PARENT_FIRST, // TODO bool
    QOS_SHAME_TIMER, // TODO 1 or 4
    ROUTE_MAP, // TODO string reference
    ROUTE_TAG_LIST, // TODO string reference
    ROUTE_TAG_NOTATION_DOTTED_DECIMAL, // TODO bool
    SAMPLER, // TODO string reference
    SASL_PROFILE, // TODO string reference
    SCRIPTING_TCL_ENCDIR, // TODO
    SCRIPTING_TCL_INIT, // TODO
    SCRIPTING_TCL_LOW_MEMORY, // TODO uint32
    SECURITY_AUTH_FAILURE_RATE_THRESHOLD, // TODO uint16
    SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG, // TODO bool
    SECURITY_PASSWORDS_MIN_LENGTH, // TODO uint8 
    SERVICE, // TODO
    SERVICE_POLICY_TYPE_CONTROL, // TODO string
    SNMP_IFMIB_IFALIAS_LONG, // TODO bool
    SNMP_IFMIB_IFINDEX_PERSIST, // TODO bool
    SNMP_IFMIB_TRAP_THROTTLE, // TODO bool
    SNMP_MIB, // TODO
    SNMP_SERVER, // TODO
    STANDBY_BFD_ALL_INTERFACES, // TODO bool
    STANDBY_REDIRECTS, // TODO bool
    TACACS_SERVER, // TODO
    TIME_RANGE, // TODO string reference
    TRACK_OBJECT, // TODO
    TRACK_RESOLUTION_IP_ROUTE_BGP, // TODO uint32
    TRACK_RESOLUTION_IP_ROUTE_EIGRP, // TODO uint32
    TRACK_RESOLUTION_IP_ROUTE_OSPF, // TODO uint32
    TRACK_RESOLUTION_IP_ROUTE_STATIC, // TODO uint32
    USERNAME, // TODO
    VRF_DEFINITION, // TODO string reference
    VRF_LIST, // TODO string reference
    VRF_SELECTION, // TODO
    WARM_REBOOT, // TODO bool
    WARM_REBOOT_COUNT, // TODO uint8
    WARM_REBOOT_UPTIME, // TODO uint8
    XCONNECT_LOGGING_PSEUDOWIRE_STATUS, // TODO bool
    XCONNECT_LOGGING_REDUNDANCY // TODO bool
};

#define GLOBAL2_DEFAULTS(X) \
    /* BANNER */ \
    X(Global2, ARCHIVE, false) \
    X(Global2, BFD_SLOW_TIMERS, 0) \
    X(Global2, CLOCK_CALENDAR_VALID, false) \
    X(Global2, CONFIG_REGISTER, 0x2102) \
    X(Global2, CONTROL_PLANE, false) \
    X(Global2, HOSTNAME, "Router") \
    /* IP */ \
    X(Global2, IP_ACCESS_LIST_HELPER_EGRESS_CHECK, false) \
    X(Global2, IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD, 0) \
    X(Global2, IP_ACCESS_LIST_LOGGING_HASH_GENERATION, false) \
    X(Global2, IP_ACCESS_LIST_LOGGING_INTERVAL, 0) \
    X(Global2, IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC, false) \
    X(Global2, IP_ACCOUNTING_THRESHOLD, false) \
    X(Global2, IP_ACCOUNTING_TRANSITS, false) \
    X(Global2, IP_ARP_INCOMPLETE, false) \
    X(Global2, IP_ARP_INCOMPLETE_ENTRIES, 0) \
    X(Global2, IP_ARP_INCOMPLETE_RETRY, 0) \
    X(Global2, IP_ARP_PROXY, false) \
    X(Global2, IP_ARP_QUEUE, 0) \
    X(Global2, IP_BGP_COMMUNITY_NEW_FORMAT, false) \
    X(Global2, IP_DEFAULT_GATEWAY, false) \
    X(Global2, IP_DOMAIN_LOOKUP_NSAP, false) \
    X(Global2, IP_DOMAIN_LOOKUP_RECURSIVE, false) \
    X(Global2, IP_DOMAIN_RECURSIVE_ALLOW_SOA, false) \
    X(Global2, IP_DOMAIN_RECURSIVE_RETRY, 0) \
    X(Global2, IP_DOMAIN_RETRY, 0) \
    X(Global2, IP_DOMAIN_ROUND_ROBIN, false) \
    X(Global2, IP_DOMAIN_TIMEOUT, 0) \
    X(Global2, IP_FLOW_CACHE_ENTRIES, 4096) \
    X(Global2, IP_FLOW_CACHE_TIMEOUT_ACTIVE, 30) \
    X(Global2, IP_FLOW_CACHE_TIMEOUT_INACTIVE, 15) \
    X(Global2, IP_FLOW_CAPTURE_FRAGMENT_OFFSET, false) \
    X(Global2, IP_FLOW_CAPTURE_ICMP, false) \
    X(Global2, IP_FLOW_CAPTURE_IP_ID, false) \
    X(Global2, IP_FLOW_CAPTURE_MAC, false) \
    X(Global2, IP_FLOW_CAPTURE_PACKET_LENGTH, false) \
    X(Global2, IP_FLOW_CAPTURE_TTL, false) \
    X(Global2, IP_FLOW_CAPTURE_VLAN_ID, false) \
    X(Global2, IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE, false) \
    X(Global2, IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS, false) \
    X(Global2, IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE, 0) \
    X(Global2, IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE, 0) \
    X(Global2, IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE, 0) \
    X(Global2, IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE, 0) \
    X(Global2, IP_FLOW_EXPORT_VERSION, 5) \
    X(Global2, IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP, false) \
    X(Global2, IP_FLOW_EXPORT_VERSION_ORIGIN_AS, false) \
    X(Global2, IP_FLOW_EXPORT_VERSION_PEER_AS, false) \
    X(Global2, IP_FLOW_TOP_TALKERS, false) \
    X(Global2, IP_HOSTNAME_STRICT, false) \
    X(Global2, IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS, 0) \
    X(Global2, IP_ICMP_RATE_LIMIT_UNREACHABLE_DF, false) \
    X(Global2, IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG, false) \
    X(Global2, IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER, 0) \
    X(Global2, IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS, 0) \
    X(Global2, IP_ICMP_REDIRECT_HOST, false) \
    X(Global2, IP_ICMP_REDIRECT_SUBNET, false) \
    X(Global2, IP_MFIB, false) \
    X(Global2, IP_OSPF_NAME_LOOKUP, false) \
    X(Global2, IP_REFLEXIVE_LIST_TIMEOUT, 0) \
    X(Global2, IP_ROUTING, true) \
    X(Global2, IP_SCP_SERVER, false) \
    X(Global2, IP_SECURITY_ESO_INFO_SOURCE, 0) \
    X(Global2, IP_SECURITY_ESO_INFO_MAX_C_BYTES, 0) \
    X(Global2, IP_SECURITY_ESO_INFO_DEFAULT_BIT, 0) \
    X(Global2, IP_STICKY_ARP, false) \
    X(Global2, IP_SUBNET_ZERO, false) \
    X(Global2, IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL, 0) \
    X(Global2, IP_VERIFY_DROP_RATE_COMPUTE_WINDOW, 0) \
    X(Global2, IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN, 0) \
    /* IPv6 */ \
    X(Global2, IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD, 0) \
    X(Global2, IPV6_FLOWSET, false) \
    X(Global2, IPV6_HOP_LIMIT, 64) \
    X(Global2, IPV6_ICMP_ERROR_INTERVAL, 0) \
    X(Global2, IPV6_ICMP_BUCKET_SIZE, 0) \
    X(Global2, IPV6_MFIB, false) \
    X(Global2, IPV6_OSPF_NAME_LOOKUP, false) \
    X(Global2, IPV6_SPD_QUEUE_MAX_THRESHOLD, 0) \
    X(Global2, IPV6_SPD_QUEUE_MIN_THRESHOLD, 0) \
    X(Global2, IPV6_TRAFFIC_INTERFACE_STATISTICS, false) \
    X(Global2, IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE, false) \
    /* L2/L3VPN */ \
    X(Global2, L2_PSEUDOWIRE_ROUTING, false) \
    /* LOGIN */ \
    X(Global2, LOGIN_BLOCK_FOR_TIME, 0) \
    X(Global2, LOGIN_BLOCK_FOR_ATTEMPTS, 0) \
    X(Global2, LOGIN_BLOCK_FOR_WITHIN, 0) \
    X(Global2, LOGIN_DELAY, 0) \
    X(Global2, LOGIN_ON_FAILURE, false) \
    X(Global2, LOGIN_ON_FAILURE_LOG, false) \
    X(Global2, LOGIN_ON_FAILURE_LOG_EVERY, 0) \
    X(Global2, LOGIN_ON_SUCCESS, false) \
    X(Global2, LOGIN_ON_SUCCESS_LOG, false) \
    X(Global2, LOGIN_ON_SUCCESS_LOG_EVERY, 0) \
    /* MLS */ \
    X(Global2, MLS_RP_IP, false) \
    X(Global2, MLS_RP_IP_INPUT_ACL, false) \
    X(Global2, MLS_RP_IP_ROUTE_MAP, false) \
    /* NETCONF */ \
    X(Global2, NETCONF_LOCKTIME, 0) \
    X(Global2, NETCONF_MAX_MESSAGE, 0) \
    X(Global2, NETCONF_MAX_SESSIONS, 0) \
    /* PASSWORD */ \
    X(Global2, PASSWORD_ENCRYPTION_AES, false) \
    X(Global2, PASSWORD_LOGGING, false) \
    /* QOS */ \
    X(Global2, QOS_POLICE_ORDER_PARENT_FIRST, false) \
    X(Global2, QOS_SHAME_TIMER, 0) \
    /* ROUTE */ \
    X(Global2, ROUTE_TAG_NOTATION_DOTTED_DECIMAL, false) \
    /* SECURITY */ \
    X(Global2, SECURITY_AUTH_FAILURE_RATE_THRESHOLD, 0) \
    X(Global2, SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG, false) \
    X(Global2, SECURITY_PASSWORDS_MIN_LENGTH, 0) \
    /* SNMP */ \
    X(Global2, SNMP_IFMIB_IFALIAS_LONG, false) \
    X(Global2, SNMP_IFMIB_IFINDEX_PERSIST, false) \
    X(Global2, SNMP_IFMIB_TRAP_THROTTLE, false) \
    /* STANDBY */ \
    X(Global2, STANDBY_BFD_ALL_INTERFACES, false) \
    X(Global2, STANDBY_REDIRECTS, false) \
    /* TRACK */ \
    X(Global2, TRACK_RESOLUTION_IP_ROUTE_BGP, 0) \
    X(Global2, TRACK_RESOLUTION_IP_ROUTE_EIGRP, 0) \
    X(Global2, TRACK_RESOLUTION_IP_ROUTE_OSPF, 0) \
    X(Global2, TRACK_RESOLUTION_IP_ROUTE_STATIC, 0) \
    /* WARM REBOOT */ \
    X(Global2, WARM_REBOOT, false) \
    X(Global2, WARM_REBOOT_COUNT, 0) \
    X(Global2, WARM_REBOOT_UPTIME, 0) \
    /* XCONNECT */ \
    X(Global2, XCONNECT_LOGGING_PSEUDOWIRE_STATUS, false) \
    X(Global2, XCONNECT_LOGGING_REDUNDANCY, false)

CONFIG_DEFAULT_TABLE(GLOBAL2_DEFAULTS);

#define IP_STANDARD_ACL_FIELDS(X) \
    X(uint32_t,    sequence) \
    X(bool,        permit) \
    X(types::IPv4Prefix, prefix) \
    X(bool,        log) \
    X(std::string, remark)

DEFINE_TUPLE_SCHEMA(IPStandardAcl, IP_STANDARD_ACL_FIELDS)

#define IP_EXTENDED_ACL_FIELDS(X) \
    X(uint32_t,    sequence) \
    X(bool,        permit) \
    X(types::IPv4Prefix, source) \
    X(types::IPv4Prefix, destination) \
    X(bool,              log) \
    X(bool,              logInput) \
    X(global::Dscp,      dscp) \
    X(bool,              fragments)
    // TODO continue

DEFINE_TUPLE_SCHEMA(IPExtendedAcl, IP_EXTENDED_ACL_FIELDS)

using Global2Registry = SubRegistry<Global2,
    // ARCHIVE
    AtomicField<bool CONFIG_INDEX_ARG(Global2::ARCHIVE)>,
    // BANNER section
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER_CONFIG_SAVE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER_EXEC)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER_INCOMING)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER_LOGIN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER_MOTD)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::BANNER_PROMPT_TIMEOUT)>,
    // BFD
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::BFD_SLOW_TIMERS)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::BFD_SINGLE_HOP_TEMPLATES)>,
    // CEF_TABLE – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::CEF_TABLE)>,
    // CLASS_MAP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::CLASS_MAP)>,
    // CLOCK
    AtomicField<bool CONFIG_INDEX_ARG(Global2::CLOCK_CALENDAR_VALID)>,
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::CLOCK_SUMMER_TIME)>,
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::CLOCK_TIME_ZONE)>,
    // CONFIG_REGISTER
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::CONFIG_REGISTER)>,
    // CONTROL_PLANE
    AtomicField<bool CONFIG_INDEX_ARG(Global2::CONTROL_PLANE)>,
    // CRYPTO – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::CRYPTO)>,
    // EAP_PROFILE
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::EAP_PROFILE)>,
    // ENABLE_PASSWORD – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::ENABLE_PASSWORD)>,
    // ENABLE_SECRET – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::ENABLE_SECRET)>,
    // FLOW exporters, monitors, records, sampler maps
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::FLOW_EXPORTER)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::FLOW_MONITOR)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::FLOW_RECORD)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::FLOW_SAMPLER_MAP)>,
    // HOSTNAME
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::HOSTNAME)>,
    // INTERFACE – interfacekey reference
    OwnedListField<InterfaceRegistry, interface::InterfaceKey CONFIG_INDEX_ARG(Global2::INTERFACE)>,
    // IP_ACCESS_LIST – incomplete
    ValueField<std::vector<std::vector<IPStandardAcl>> CONFIG_INDEX_ARG(Global2::IP_ACCESS_LIST_EXTENDED)>,
    AtomicField<bool CONFIG_INDEX_ARG(IP_ACCESS_LIST_HELPER_EGRESS_CHECK)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(IP_ACCESS_LIST_LOGGING_HASH_GENERATION)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(IP_ACCESS_LIST_LOGGING_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC)>,
    ValueField<Incomplete CONFIG_INDEX_ARG(IP_ACCESS_LIST_ROLE_BASED)>,
    ValueField<std::vector<std::vector<IPExtendedAcl>> CONFIG_INDEX_ARG(IP_ACCESS_LIST_STANDARD)>,
    // IP_ACCOUNTING_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_ACCOUNTING_LIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ACCOUNTING_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ACCOUNTING_TRANSITS)>,
    // IP_ADDRESS_POOL – AddressPoolMode (enum)
    AtomicField<int CONFIG_INDEX_ARG(Global2::IP_ADDRESS_POOL)>,
    // IP_ARP_GRATUITOUS – GratuitousType (enum)
    AtomicField<int CONFIG_INDEX_ARG(Global2::IP_ARP_GRATUITOUS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ARP_INCOMPLETE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_ARP_INCOMPLETE_ENTRIES)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_ARP_INCOMPLETE_RETRY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ARP_PROXY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_ARP_QUEUE)>,
    // IP_AS_PATH_ACCESS_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_AS_PATH_ACCESS_LIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_BGP_COMMUNITY_NEW_FORMAT)>,
    // IP_CEF – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_CEF)>,
    // IP_CLASSLESS – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_CLASSLESS)>,
    // IP_COMMUNITY_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_COMMUNITY_LIST)>,
    // IP_DEFAULT_NETWORK – ipv4
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_DEFAULT_NETWORK)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_DEFAULT_GATEWAY)>,
    // IP_DHCP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_DHCP)>,
    // IP_DHCP_CLIENT – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_DHCP_CLIENT)>,
    // IP_DHCP_RELAY – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_DHCP_RELAY)>,
    // IP_DHCP_SERVER – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_DHCP_SERVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_DOMAIN_LOOKUP_NSAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_DOMAIN_LOOKUP_RECURSIVE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::IP_DOMAIN_MULTICAST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_DOMAIN_RECURSIVE_ALLOW_SOA)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_DOMAIN_RECURSIVE_RETRY)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_DOMAIN_RETRY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_DOMAIN_ROUND_ROBIN)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_DOMAIN_TIMEOUT)>,
    // IP_EXPLICIT_PATH_IDENTIFIER – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::IP_EXPLICIT_PATH_IDENTIFIER)>,
    // IP_EXPLICIT_PATH_NAME – int reference
    OwnedListField<EmptyRegistry, int CONFIG_INDEX_ARG(Global2::IP_EXPLICIT_PATH_NAME)>,
    // IP_EXTCOMMUNITY_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_EXTCOMMUNITY_LIST)>,
    // IP_FLOW_AGGREGATION_CACHE – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_FLOW_AGGREGATION_CACHE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_FLOW_CACHE_ENTRIES)>,
    // IP_FLOW_CACHE_MPLS – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_FLOW_CACHE_MPLS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_FLOW_CACHE_TIMEOUT_ACTIVE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_FLOW_CACHE_TIMEOUT_INACTIVE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_FRAGMENT_OFFSET)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_ICMP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_IP_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_MAC)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_PACKET_LENGTH)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_TTL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_VLAN_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE)>,
    // IP_FLOW_EXPORT_DESTINATION – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_DESTINATION)>,
    // IP_FLOW_EXPORT_SOURCE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_SOURCE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_VERSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_VERSION_ORIGIN_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_EXPORT_VERSION_PEER_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_FLOW_TOP_TALKERS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_HOSTNAME_STRICT)>,
    // IP_HTTP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_HTTP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ICMP_RATE_LIMIT_UNREACHABLE_DF)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ICMP_REDIRECT_HOST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ICMP_REDIRECT_SUBNET)>,
    // IP_KERBEROS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global2::IP_KERBEROS_SOURCE_INTERFACE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::IP_LOCAL_POLICY_ROUTE_MAP)>,
    // IP_LOCAL_POOL – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_LOCAL_POOL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_MFIB)>,
    // IP_NAT – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_NAT)>,
    // IP_NBAR – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_NBAR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_OSPF_NAME_LOOKUP)>,
    // IP_POLICY_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_POLICY_LIST)>,
    // IP_PREFIX_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_PREFIX_LIST)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IP_REFLEXIVE_LIST_TIMEOUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_ROUTING)>,
    // IP_ROUTING_PROTOCOL_PURGE_INTERFACE – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_ROUTING_PROTOCOL_PURGE_INTERFACE)>,
    // IP_RSVP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_RSVP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_SCP_SERVER)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_SECURITY_ESO_INFO_SOURCE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_SECURITY_ESO_INFO_MAX_C_BYTES)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IP_SECURITY_ESO_INFO_DEFAULT_BIT)>,
    // IP_SLA – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_SLA)>,
    // IP_SSH – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_SSH)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_STICKY_ARP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IP_SUBNET_ZERO)>,
    // IP_TACACS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global2::IP_TACACS_SOURCE_INTERFACE)>,
    // IP_TCP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_TCP)>,
    // IP_TELNET – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_TELNET)>,
    // IP_TFTP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IP_TFTP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::IP_TRAFFIC_EXPORT_PROFILE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_VERIFY_DROP_RATE_COMPUTE_WINDOW)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN)>,
    // IP_VRF – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::IP_VRF)>,
    // IPv6
    // IPV6_ACCESS_LIST – word reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::IPV6_ACCESS_LIST)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD)>,
    // IPV6_ACCESS_LIST_ROLE_BASED – word reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::IPV6_ACCESS_LIST_ROLE_BASED)>,
    // IPV6_CEF – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_CEF)>,
    // IPV6_DHCP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_DHCP)>,
    // IPV6_DHCP_CLIENT – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_DHCP_CLIENT)>,
    // IPV6_DHCP_RELAY – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_DHCP_RELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IPV6_FLOWSET)>,
    // IPV6_GENERAL_PREFIX – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_GENERAL_PREFIX)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::IPV6_HOP_LIMIT)>,
    // IPV6_HOST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_HOST)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::IPV6_ICMP_ERROR_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IPV6_ICMP_BUCKET_SIZE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::IPV6_LOCAL_POLICY_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IPV6_MFIB)>,
    // IPV6_ND – reference
    OwnedListField<NdpBaseRegistry, Incomplete CONFIG_INDEX_ARG(Global2::IPV6_ND)>,
    // IPV6_NEIGHBOR – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_NEIGHBOR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IPV6_OSPF_NAME_LOOKUP)>,
    // IPV6_PREFIX_LIST – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_PREFIX_LIST)>,
    // IPV6_PREFIX_POOL – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::IPV6_PREFIX_POOL)>,
    // IPV6_RADIUS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global2::IPV6_RADIUS_SOURCE_INTERFACE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IPV6_SPD_QUEUE_MAX_THRESHOLD)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::IPV6_SPD_QUEUE_MIN_THRESHOLD)>,
    // IPV6_TACACS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global2::IPV6_TACACS_SOURCE_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IPV6_TRAFFIC_INTERFACE_STATISTICS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE)>,
    // KERBEROS – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::KERBEROS)>,
    // KEY_CHAIN – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::KEY_CHAIN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::KEY_CONFIG_KEY)>,
    // KRON – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::KRON)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::L2_PSEUDOWIRE_ROUTING)>,
    // L2_ROUTER_ID – ipv4
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global2::L2_ROUTER_ID)>,
    // L2_VFI – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::L2_VFI)>,
    // L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS)>,
    // L2VPN_VFI_CONTEXT – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::L2VPN_VFI_CONTEXT)>,
    // L2VPN_XCONNECT_CONTEXT – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::L2VPN_XCONNECT_CONTEXT)>,
    // L3VPN_ENCAPSULATION_IP_PROFILE – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::L3VPN_ENCAPSULATION_IP_PROFILE)>,
    // LINE_RANGE
    ValueField<std::pair<uint16_t, uint16_t> CONFIG_INDEX_ARG(Global2::LINE_RANGE)>,
    // LINE_AUX, LINE_CONSOLE, LINE_VTY – uint16 references
    OwnedListField<EmptyRegistry, uint16_t CONFIG_INDEX_ARG(Global2::LINE_AUX)>,
    OwnedListField<EmptyRegistry, uint16_t CONFIG_INDEX_ARG(Global2::LINE_CONSOLE)>,
    OwnedListField<EmptyRegistry, uint16_t CONFIG_INDEX_ARG(Global2::LINE_VTY)>,
    // LOGGING – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::LOGGING)>,
    // LOGIN
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::LOGIN_BLOCK_FOR_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::LOGIN_BLOCK_FOR_ATTEMPTS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::LOGIN_BLOCK_FOR_WITHIN)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::LOGIN_DELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::LOGIN_ON_FAILURE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::LOGIN_ON_FAILURE_LOG)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::LOGIN_ON_FAILURE_LOG_EVERY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::LOGIN_ON_SUCCESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::LOGIN_ON_SUCCESS_LOG)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::LOGIN_ON_SUCCESS_LOG_EVERY)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::LOGIN_QUITE_MODE_ACCESS_CLASS)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::LOGIN_STRING_NAME)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::LOGIN_STRING_LINE)>,
    // MLS
    AtomicField<bool CONFIG_INDEX_ARG(Global2::MLS_RP_IP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::MLS_RP_IP_INPUT_ACL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::MLS_RP_IP_ROUTE_MAP)>,
    // MLS_RP_NDE_ADDRESS – ipv4
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global2::MLS_RP_NDE_ADDRESS)>,
    // MONITOR_EVENT_TRACE – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::MONITOR_EVENT_TRACE)>,
    // MPLS – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::MPLS)>,
    // NETCONF
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::NETCONF_FORMAT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::NETCONF_LOCKTIME)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::NETCONF_MAX_MESSAGE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::NETCONF_MAX_SESSIONS)>,
    // NTP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::NTP)>,
    // OBJECT_GROUP_SECURITY – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::OBJECT_GROUP_SECURITY)>,
    // PASSWORD
    AtomicField<bool CONFIG_INDEX_ARG(Global2::PASSWORD_ENCRYPTION_AES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::PASSWORD_LOGGING)>,
    // POLICY_MAP – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::POLICY_MAP)>,
    // PRIVILEGED – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::PRIVILEGED)>,
    // QOS
    AtomicField<bool CONFIG_INDEX_ARG(Global2::QOS_POLICE_ORDER_PARENT_FIRST)>,
    AtomicField<int CONFIG_INDEX_ARG(Global2::QOS_SHAME_TIMER)>, // 1 or 4
    // ROUTE_MAP – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::ROUTE_MAP)>,
    // ROUTE_TAG_LIST – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::ROUTE_TAG_LIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::ROUTE_TAG_NOTATION_DOTTED_DECIMAL)>,
    // SAMPLER – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::SAMPLER)>,
    // SASL_PROFILE – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::SASL_PROFILE)>,
    // SCRIPTING
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::SCRIPTING_TCL_ENCDIR)>,
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::SCRIPTING_TCL_INIT)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::SCRIPTING_TCL_LOW_MEMORY)>,
    // SECURITY
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global2::SECURITY_AUTH_FAILURE_RATE_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::SECURITY_PASSWORDS_MIN_LENGTH)>,
    // SERVICE – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::SERVICE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global2::SERVICE_POLICY_TYPE_CONTROL)>,
    // SNMP
    AtomicField<bool CONFIG_INDEX_ARG(Global2::SNMP_IFMIB_IFALIAS_LONG)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::SNMP_IFMIB_IFINDEX_PERSIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::SNMP_IFMIB_TRAP_THROTTLE)>,
    // SNMP_MIB – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::SNMP_MIB)>,
    // SNMP_SERVER – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::SNMP_SERVER)>,
    // STANDBY
    AtomicField<bool CONFIG_INDEX_ARG(Global2::STANDBY_BFD_ALL_INTERFACES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::STANDBY_REDIRECTS)>,
    // TACACS_SERVER – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::TACACS_SERVER)>,
    // TIME_RANGE – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::TIME_RANGE)>,
    // TRACK_OBJECT – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::TRACK_OBJECT)>,
    // TRACK_RESOLUTION
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::TRACK_RESOLUTION_IP_ROUTE_BGP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::TRACK_RESOLUTION_IP_ROUTE_EIGRP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::TRACK_RESOLUTION_IP_ROUTE_OSPF)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global2::TRACK_RESOLUTION_IP_ROUTE_STATIC)>,
    // USERNAME – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::USERNAME)>,
    // VRF
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::VRF_DEFINITION)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global2::VRF_LIST)>,
    // VRF_SELECTION – incomplete
    AtomicField<Incomplete CONFIG_INDEX_ARG(Global2::VRF_SELECTION)>,
    // WARM REBOOT
    AtomicField<bool CONFIG_INDEX_ARG(Global2::WARM_REBOOT)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::WARM_REBOOT_COUNT)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global2::WARM_REBOOT_UPTIME)>,
    // XCONNECT
    AtomicField<bool CONFIG_INDEX_ARG(Global2::XCONNECT_LOGGING_PSEUDOWIRE_STATUS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global2::XCONNECT_LOGGING_REDUNDANCY)>
>;

enum class Global
{
    VRF_CONFIGS,
    INTERFACES
};

using GlobalRegistry = SubRegistry<Global,
    OwnedListField<VrfRegistry, std::string CONFIG_INDEX_ARG(Global::VRF_CONFIGS)>,
    OwnedListField<InterfaceRegistry, interface::InterfaceKey CONFIG_INDEX_ARG(Global::INTERFACES)>
>;
}

#endif // GLOBAL_REGISTRY_HPP
