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
#include "configs/RegistryReference.hpp"
#include "VrfRegistry.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "configs/TupleSchema.hpp"
#include "interface/configs/InterfaceType.hpp"

#undef IP_TCP
#undef IP_RSVP

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

enum class IPOption : uint8_t
{

};
}

enum class Global
{
    ARCHIVE, // TODO
    BANNER, // TODO
    BANNER_CONFIG_SAVE, // TODO
    BANNER_EXEC, // TODO
    BANNER_INCOMING, // TODO
    BANNER_LOGIN, // TODO
    BANNER_MOTD, // TODO
    BANNER_PROMPT_TIMEOUT, // TODO
    BFD_SLOW_TIMERS, // TODO
    BFD_SINGLE_HOP_TEMPLATES, // TODO
    CEF_TABLE, // TODO
    CLASS_MAP, // TODO
    CLOCK_CALENDAR_VALID, // TODO
    CLOCK_SUMMER_TIME, // TODO
    CLOCK_TIME_ZONE, // TODO
    CONFIG_REGISTER, // TODO
    CONTROL_PLANE, // TODO
    CRYPTO, // TODO
    EAP_PROFILE, // TODO
    ENABLE_PASSWORD, // TODO
    ENABLE_SECRET, // TODO
    FLOW_EXPORTER, // TODO
    FLOW_MONITOR, // TODO
    FLOW_RECORD, // TODO
    FLOW_SAMPLER_MAP, // TODO
    HOSTNAME, // TODO
    INTERFACE,
    IP_ACCESS_LIST_EXTENDED, // TODO
    IP_ACCESS_LIST_HELPER_EGRESS_CHECK, // TODO
    IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD, // TODO
    IP_ACCESS_LIST_LOGGING_HASH_GENERATION, // TODO
    IP_ACCESS_LIST_LOGGING_INTERVAL, // TODO
    IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC, // TODO
    IP_ACCESS_LIST_ROLE_BASED, // TODO
    IP_ACCESS_LIST_STANDARD, // TODO
    IP_ACCOUNTING_LIST, // TODO
    IP_ACCOUNTING_THRESHOLD, // TODO
    IP_ACCOUNTING_TRANSITS, // TODO
    IP_ADDRESS_POOL_DHCP, // TODO
    IP_ADDRESS_POOL_DHCP_PROXY, // TODO
    IP_ADDRESS_POOL_LOCAL, // TODO
    IP_ARP_GRATUITOUS, // TODO
    IP_ARP_INCOMPLETE, // TODO
    IP_ARP_INCOMPLETE_ENTRIES, // TODO
    IP_ARP_INCOMPLETE_RETRY, // TODO
    IP_ARP_PROXY, // TODO
    IP_ARP_QUEUE, // TODO
    IP_AS_PATH_ACCESS_LIST, // TODO
    IP_BGP_COMMUNITY_NEW_FORMAT, // TODO
    IP_CEF, // TODO
    IP_CLASSLESS, // TODO
    IP_COMMUNITY_LIST, // TODO
    IP_DEFAULT_NETWORK, // TODO
    IP_DEFAULT_GATEWAY, // TODO
    IP_DHCP, // TODO
    IP_DHCP_CLIENT, // TODO
    IP_DHCP_RELAY, // TODO
    IP_DHCP_SERVER, // TODO
    IP_DOMAIN_LOOKUP_NSAP, // TODO
    IP_DOMAIN_LOOKUP_RECURSIVE, // TODO
    IP_DOMAIN_MULTICAST, // TODO
    IP_DOMAIN_RECURSIVE_ALLOW_SOA, // TODO
    IP_DOMAIN_RECURSIVE_RETRY, // TODO
    IP_DOMAIN_RETRY, // TODO
    IP_DOMAIN_ROUND_ROBIN, // TODO
    IP_DOMAIN_TIMEOUT, // TODO
    IP_EXPLICIT_PATH_IDENTIFIER, // TODO
    IP_EXPLICIT_PATH_NAME, // TODO
    IP_EXTCOMMUNITY_LIST, // TODO
    IP_FLOW_AGGREGATION_CACHE, // TODO
    IP_FLOW_CACHE_ENTRIES, // TODO
    IP_FLOW_CACHE_MPLS, // TODO
    IP_FLOW_CACHE_TIMEOUT_ACTIVE, // TODO
    IP_FLOW_CACHE_TIMEOUT_INACTIVE, // TODO
    IP_FLOW_CAPTURE_FRAGMENT_OFFSET, // TODO
    IP_FLOW_CAPTURE_ICMP, // TODO
    IP_FLOW_CAPTURE_IP_ID, // TODO
    IP_FLOW_CAPTURE_MAC, // TODO
    IP_FLOW_CAPTURE_PACKET_LENGTH, // TODO
    IP_FLOW_CAPTURE_TTL, // TODO
    IP_FLOW_CAPTURE_VLAN_ID, // TODO
    IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE, // TODO
    IP_FLOW_EXPORT_DESTINATION, // TODO
    IP_FLOW_EXPORT_SOURCE, // TODO
    IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS, // TODO
    IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE, // TODO
    IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE, // TODO
    IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE, // TODO
    IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE, // TODO
    IP_FLOW_EXPORT_VERSION, // TODO
    IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP, // TODO
    IP_FLOW_EXPORT_VERSION_ORIGIN_AS, // TODO
    IP_FLOW_EXPORT_VERSION_PEER_AS, // TODO
    IP_FLOW_TOP_TALKERS, // TODO
    IP_HOSTNAME_STRICT, // TODO
    IP_HTTP, // TODO
    IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS, // TODO
    IP_ICMP_RATE_LIMIT_UNREACHABLE_DF, // TODO
    IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG, // TODO
    IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER, // TODO
    IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS, // TODO
    IP_ICMP_REDIRECT_HOST, // TODO
    IP_ICMP_REDIRECT_SUBNET, // TODO
    IP_KERBEROS_SOURCE_INTERFACE, // TODO
    IP_LOCAL_POLICY_ROUTE_MAP, // TODO
    IP_LOCAL_POOL, // TODO
    IP_MFIB, // TODO
    IP_NAT, // TODO
    IP_NBAR, // TODO
    IP_OSPF_NAME_LOOKUP, // TODO
    IP_POLICY_LIST, // TODO
    IP_PREFIX_LIST, // TODO
    IP_REFLEXIVE_LIST_TIMEOUT, // TODO
    IP_ROUTING, // TODO
    IP_ROUTING_PROTOCOL_PURGE_INTERFACE, // TODO
    IP_RSVP, // TODO
    IP_SCP_SERVER, // TODO
    IP_SECURITY_ESO_INFO_SOURCE, // TODO
    IP_SECURITY_ESO_INFO_MAX_C_BYTES, // TODO
    IP_SECURITY_ESO_INFO_DEFAULT_BIT, // TODO
    IP_SLA, // TODO
    IP_SSH, // TODO
    IP_STICKY_ARP, // TODO
    IP_SUBNET_ZERO, // TODO
    IP_TACACS_SOURCE_INTERFACE, // TODO
    IP_TCP, // TODO
    IP_TELNET, // TODO
    IP_TFTP, // TODO
    IP_TRAFFIC_EXPORT_PROFILE, // TODO
    IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL, // TODO
    IP_VERIFY_DROP_RATE_COMPUTE_WINDOW, // TODO
    IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN, // TODO
    IP_VRF, // TODO
    IPV6_ACCESS_LIST, // TODO
    IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD, // TODO
    IPV6_ACCESS_LIST_ROLE_BASED, // TODO
    IPV6_CEF, // TODO
    IPV6_DHCP, // TODO
    IPV6_DHCP_CLIENT, // TODO
    IPV6_DHCP_RELAY, // TODO
    IPV6_FLOWSET, // TODO
    IPV6_GENERAL_PREFIX, // TODO
    IPV6_HOP_LIMIT, // TODO
    IPV6_HOST, // TODO
    IPV6_ICMP_ERROR_INTERVAL, // TODO
    IPV6_ICMP_BUCKET_SIZE, //TODO uint16
    IPV6_LOCAL_POLICY_ROUTE_MAP, // TODO
    IPV6_MFIB, // TODO
    IPV6_ND, // TODO
    IPV6_NEIGHBOR, // TODO
    IPV6_OSPF_NAME_LOOKUP, // TODO
    IPV6_PREFIX_LIST, // TODO
    IPV6_PREFIX_POOL, // TODO
    IPV6_RADIUS_SOURCE_INTERFACE, // TODO
    IPV6_SPD_QUEUE_MAX_THRESHOLD, // TODO
    IPV6_SPD_QUEUE_MIN_THRESHOLD, // TODO
    IPV6_TACACS_SOURCE_INTERFACE, // TODO
    IPV6_TRAFFIC_INTERFACE_STATISTICS, // TODO
    IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE, // TODO
    KERBEROS, // TODO
    KEY_CHAIN, // TODO
    KEY_CONFIG_KEY, // TODO
    KRON, // TODO
    L2_PSEUDOWIRE_ROUTING, // TODO
    L2_ROUTER_ID, // TODO
    L2_VFI, // TODO
    L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS, // TODO
    L2VPN_VFI_CONTEXT, // TODO
    L2VPN_XCONNECT_CONTEXT, // TODO
    L3VPN_ENCAPSULATION_IP_PROFILE, // TODO
    LINE_RANGE, // TODO
    LINE_AUX, // TODO
    LINE_CONSOLE, // TODO
    LINE_VTY, // TODO
    LOGGING, // TODO
    LOGIN_BLOCK_FOR_TIME, // TODO
    LOGIN_BLOCK_FOR_ATTEMPTS, // TODO
    LOGIN_BLOCK_FOR_WITHIN, // TODO
    LOGIN_DELAY, // TODO
    LOGIN_ON_FAILURE, // TODO
    LOGIN_ON_FAILURE_LOG, // TODO
    LOGIN_ON_FAILURE_LOG_EVERY, // TODO
    LOGIN_ON_SUCCESS, // TODO
    LOGIN_ON_SUCCESS_LOG, // TODO
    LOGIN_ON_SUCCESS_LOG_EVERY, // TODO
    LOGIN_QUITE_MODE_ACCESS_CLASS, // TODO
    LOGIN_STRING_NAME, // TODO
    LOGIN_STRING_LINE, // TODO
    MLS_RP_IP, // TODO
    MLS_RP_IP_INPUT_ACL, // TODO
    MLS_RP_IP_ROUTE_MAP, // TODO
    MLS_RP_NDE_ADDRESS, // TODO
    MONITOR_EVENT_TRACE, // TODO
    MPLS, // TODO
    NETCONF_FORMAT, // TODO
    NETCONF_LOCKTIME, // TODO
    NETCONF_MAX_MESSAGE, // TODO
    NETCONF_MAX_SESSIONS, // TODO
    NTP, // TODO
    OBJECT_GROUP_SECURITY, // TODO
    PASSWORD_ENCRYPTION_AES, // TODO
    PASSWORD_LOGGING, // TODO
    POLICY_MAP, // TODO
    PRIVILEGED, // TODO
    QOS_POLICE_ORDER_PARENT_FIRST, // TODO
    QOS_SHAME_TIMER, // TODO
    ROUTE_MAP, // TODO
    ROUTE_TAG_LIST, // TODO
    ROUTE_TAG_NOTATION_DOTTED_DECIMAL, // TODO
    SAMPLER, // TODO
    SASL_PROFILE, // TODO
    SCRIPTING_TCL_ENCDIR, // TODO
    SCRIPTING_TCL_INIT, // TODO
    SCRIPTING_TCL_LOW_MEMORY, // TODO
    SECURITY_AUTH_FAILURE_RATE_THRESHOLD, // TODO
    SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG, // TODO
    SECURITY_PASSWORDS_MIN_LENGTH, // TODO
    SERVICE, // TODO
    SERVICE_POLICY_TYPE_CONTROL, // TODO
    SNMP_IFMIB_IFALIAS_LONG, // TODO
    SNMP_IFMIB_IFINDEX_PERSIST, // TODO
    SNMP_IFMIB_TRAP_THROTTLE, // TODO
    SNMP_MIB, // TODO
    SNMP_SERVER, // TODO
    STANDBY_BFD_ALL_INTERFACES, // TODO
    STANDBY_REDIRECTS, // TODO
    TACACS_SERVER, // TODO
    TIME_RANGE, // TODO
    TRACK_OBJECT, // TODO
    TRACK_RESOLUTION_IP_ROUTE_BGP, // TODO
    TRACK_RESOLUTION_IP_ROUTE_EIGRP, // TODO
    TRACK_RESOLUTION_IP_ROUTE_OSPF, // TODO
    TRACK_RESOLUTION_IP_ROUTE_STATIC, // TODO
    USERNAME, // TODO
    VRF_CONFIGS,
    VRF_LIST, // TODO
    VRF_SELECTION, // TODO
    WARM_REBOOT, // TODO
    WARM_REBOOT_COUNT, // TODO
    WARM_REBOOT_UPTIME, // TODO
    XCONNECT_LOGGING_PSEUDOWIRE_STATUS, // TODO
    XCONNECT_LOGGING_REDUNDANCY, // TODO
    COUNT
};

#define GLOBAL_DEFAULTS(X) \
    /* BANNER */ \
    X(Global, ARCHIVE, false) \
    X(Global, BFD_SLOW_TIMERS, 0) \
    X(Global, CLOCK_CALENDAR_VALID, false) \
    X(Global, CONFIG_REGISTER, 0x2102) \
    X(Global, CONTROL_PLANE, false) \
    X(Global, HOSTNAME, "Router") \
    /* IP */ \
    X(Global, IP_ACCESS_LIST_HELPER_EGRESS_CHECK, false) \
    X(Global, IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD, 0) \
    X(Global, IP_ACCESS_LIST_LOGGING_HASH_GENERATION, false) \
    X(Global, IP_ACCESS_LIST_LOGGING_INTERVAL, 0) \
    X(Global, IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC, false) \
    X(Global, IP_ACCOUNTING_THRESHOLD, false) \
    X(Global, IP_ACCOUNTING_TRANSITS, false) \
    X(Global, IP_ADDRESS_POOL_DHCP, false) \
    X(Global, IP_ADDRESS_POOL_DHCP_PROXY, false) \
    X(Global, IP_ADDRESS_POOL_LOCAL, true) \
    X(Global, IP_ARP_GRATUITOUS, true) \
    X(Global, IP_ARP_INCOMPLETE, false) \
    X(Global, IP_ARP_INCOMPLETE_RETRY, 3) \
    X(Global, IP_ARP_PROXY, false) \
    X(Global, IP_ARP_QUEUE, 512) \
    X(Global, IP_BGP_COMMUNITY_NEW_FORMAT, false) \
    X(Global, IP_DEFAULT_GATEWAY, false) \
    X(Global, IP_DOMAIN_LOOKUP_NSAP, false) \
    X(Global, IP_DOMAIN_LOOKUP_RECURSIVE, false) \
    X(Global, IP_DOMAIN_RECURSIVE_ALLOW_SOA, false) \
    X(Global, IP_DOMAIN_RECURSIVE_RETRY, 0) \
    X(Global, IP_DOMAIN_RETRY, 0) \
    X(Global, IP_DOMAIN_ROUND_ROBIN, false) \
    X(Global, IP_DOMAIN_TIMEOUT, 0) \
    X(Global, IP_FLOW_CACHE_ENTRIES, 4096) \
    X(Global, IP_FLOW_CACHE_TIMEOUT_ACTIVE, 30) \
    X(Global, IP_FLOW_CACHE_TIMEOUT_INACTIVE, 15) \
    X(Global, IP_FLOW_CAPTURE_FRAGMENT_OFFSET, false) \
    X(Global, IP_FLOW_CAPTURE_ICMP, false) \
    X(Global, IP_FLOW_CAPTURE_IP_ID, false) \
    X(Global, IP_FLOW_CAPTURE_MAC, false) \
    X(Global, IP_FLOW_CAPTURE_PACKET_LENGTH, false) \
    X(Global, IP_FLOW_CAPTURE_TTL, false) \
    X(Global, IP_FLOW_CAPTURE_VLAN_ID, false) \
    X(Global, IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE, false) \
    X(Global, IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS, false) \
    X(Global, IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE, 0) \
    X(Global, IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE, 0) \
    X(Global, IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE, 0) \
    X(Global, IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE, 0) \
    X(Global, IP_FLOW_EXPORT_VERSION, 5) \
    X(Global, IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP, false) \
    X(Global, IP_FLOW_EXPORT_VERSION_ORIGIN_AS, false) \
    X(Global, IP_FLOW_EXPORT_VERSION_PEER_AS, false) \
    X(Global, IP_FLOW_TOP_TALKERS, false) \
    X(Global, IP_HOSTNAME_STRICT, false) \
    X(Global, IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS, 0) \
    X(Global, IP_ICMP_RATE_LIMIT_UNREACHABLE_DF, false) \
    X(Global, IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG, false) \
    X(Global, IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER, 0) \
    X(Global, IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS, 0) \
    X(Global, IP_ICMP_REDIRECT_HOST, false) \
    X(Global, IP_ICMP_REDIRECT_SUBNET, false) \
    X(Global, IP_MFIB, false) \
    X(Global, IP_OSPF_NAME_LOOKUP, false) \
    X(Global, IP_REFLEXIVE_LIST_TIMEOUT, 0) \
    X(Global, IP_ROUTING, true) \
    X(Global, IP_SCP_SERVER, false) \
    X(Global, IP_SECURITY_ESO_INFO_SOURCE, 0) \
    X(Global, IP_SECURITY_ESO_INFO_MAX_C_BYTES, 0) \
    X(Global, IP_SECURITY_ESO_INFO_DEFAULT_BIT, 0) \
    X(Global, IP_STICKY_ARP, false) \
    X(Global, IP_SUBNET_ZERO, false) \
    X(Global, IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL, 0) \
    X(Global, IP_VERIFY_DROP_RATE_COMPUTE_WINDOW, 0) \
    X(Global, IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN, 0) \
    /* IPv6 */ \
    X(Global, IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD, 0) \
    X(Global, IPV6_FLOWSET, false) \
    X(Global, IPV6_HOP_LIMIT, 64) \
    X(Global, IPV6_ICMP_ERROR_INTERVAL, 0) \
    X(Global, IPV6_ICMP_BUCKET_SIZE, 0) \
    X(Global, IPV6_MFIB, false) \
    X(Global, IPV6_OSPF_NAME_LOOKUP, false) \
    X(Global, IPV6_SPD_QUEUE_MAX_THRESHOLD, 0) \
    X(Global, IPV6_SPD_QUEUE_MIN_THRESHOLD, 0) \
    X(Global, IPV6_TRAFFIC_INTERFACE_STATISTICS, false) \
    X(Global, IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE, false) \
    /* L2/L3VPN */ \
    X(Global, L2_PSEUDOWIRE_ROUTING, false) \
    /* LOGIN */ \
    X(Global, LOGIN_BLOCK_FOR_TIME, 0) \
    X(Global, LOGIN_BLOCK_FOR_ATTEMPTS, 0) \
    X(Global, LOGIN_BLOCK_FOR_WITHIN, 0) \
    X(Global, LOGIN_DELAY, 0) \
    X(Global, LOGIN_ON_FAILURE, false) \
    X(Global, LOGIN_ON_FAILURE_LOG, false) \
    X(Global, LOGIN_ON_FAILURE_LOG_EVERY, 0) \
    X(Global, LOGIN_ON_SUCCESS, false) \
    X(Global, LOGIN_ON_SUCCESS_LOG, false) \
    X(Global, LOGIN_ON_SUCCESS_LOG_EVERY, 0) \
    /* MLS */ \
    X(Global, MLS_RP_IP, false) \
    X(Global, MLS_RP_IP_INPUT_ACL, false) \
    X(Global, MLS_RP_IP_ROUTE_MAP, false) \
    /* NETCONF */ \
    X(Global, NETCONF_LOCKTIME, 0) \
    X(Global, NETCONF_MAX_MESSAGE, 0) \
    X(Global, NETCONF_MAX_SESSIONS, 0) \
    /* PASSWORD */ \
    X(Global, PASSWORD_ENCRYPTION_AES, false) \
    X(Global, PASSWORD_LOGGING, false) \
    /* QOS */ \
    X(Global, QOS_POLICE_ORDER_PARENT_FIRST, false) \
    X(Global, QOS_SHAME_TIMER, 0) \
    /* ROUTE */ \
    X(Global, ROUTE_TAG_NOTATION_DOTTED_DECIMAL, false) \
    /* SECURITY */ \
    X(Global, SECURITY_AUTH_FAILURE_RATE_THRESHOLD, 0) \
    X(Global, SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG, false) \
    X(Global, SECURITY_PASSWORDS_MIN_LENGTH, 0) \
    /* SNMP */ \
    X(Global, SNMP_IFMIB_IFALIAS_LONG, false) \
    X(Global, SNMP_IFMIB_IFINDEX_PERSIST, false) \
    X(Global, SNMP_IFMIB_TRAP_THROTTLE, false) \
    /* STANDBY */ \
    X(Global, STANDBY_BFD_ALL_INTERFACES, false) \
    X(Global, STANDBY_REDIRECTS, false) \
    /* TRACK */ \
    X(Global, TRACK_RESOLUTION_IP_ROUTE_BGP, 0) \
    X(Global, TRACK_RESOLUTION_IP_ROUTE_EIGRP, 0) \
    X(Global, TRACK_RESOLUTION_IP_ROUTE_OSPF, 0) \
    X(Global, TRACK_RESOLUTION_IP_ROUTE_STATIC, 0) \
    /* WARM REBOOT */ \
    X(Global, WARM_REBOOT, false) \
    X(Global, WARM_REBOOT_COUNT, 0) \
    X(Global, WARM_REBOOT_UPTIME, 0) \
    /* XCONNECT */ \
    X(Global, XCONNECT_LOGGING_PSEUDOWIRE_STATUS, false) \
    X(Global, XCONNECT_LOGGING_REDUNDANCY, false)

CONFIG_DEFAULT_TABLE(GLOBAL_DEFAULTS);

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


using GlobalRegistry = SubRegistry<Global,
    // ARCHIVE
    AtomicField<bool CONFIG_INDEX_ARG(Global::ARCHIVE)>,
    // BANNER section
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER_CONFIG_SAVE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER_EXEC)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER_INCOMING)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER_LOGIN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER_MOTD)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::BANNER_PROMPT_TIMEOUT)>,
    // BFD
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::BFD_SLOW_TIMERS)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::BFD_SINGLE_HOP_TEMPLATES)>,
    // CEF_TABLE – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::CEF_TABLE)>,
    // CLASS_MAP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::CLASS_MAP)>,
    // CLOCK
    AtomicField<bool CONFIG_INDEX_ARG(Global::CLOCK_CALENDAR_VALID)>,
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::CLOCK_SUMMER_TIME)>,
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::CLOCK_TIME_ZONE)>,
    // CONFIG_REGISTER
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::CONFIG_REGISTER)>,
    // CONTROL_PLANE
    AtomicField<bool CONFIG_INDEX_ARG(Global::CONTROL_PLANE)>,
    // CRYPTO – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::CRYPTO)>,
    // EAP_PROFILE
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::EAP_PROFILE)>,
    // ENABLE_PASSWORD – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::ENABLE_PASSWORD)>,
    // ENABLE_SECRET – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::ENABLE_SECRET)>,
    // FLOW exporters, monitors, records, sampler maps
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::FLOW_EXPORTER)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::FLOW_MONITOR)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::FLOW_RECORD)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::FLOW_SAMPLER_MAP)>,
    // HOSTNAME
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::HOSTNAME)>,
    // INTERFACE – interfacekey reference
    OwnedListField<InterfaceRegistry, interface::InterfaceKey CONFIG_INDEX_ARG(Global::INTERFACE)>,
    // IP_ACCESS_LIST – incomplete
    ValueField<std::vector<std::vector<IPStandardAcl>> CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_EXTENDED)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_HELPER_EGRESS_CHECK)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_LOG_UPDATE_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_LOGGING_HASH_GENERATION)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_LOGGING_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_MATCH_LOCAL_TRAFFIC)>,
    OptionalValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_ROLE_BASED)>,
    OptionalValueField<std::vector<std::vector<IPExtendedAcl>> CONFIG_INDEX_ARG(Global::IP_ACCESS_LIST_STANDARD)>,
    // IP_ACCOUNTING_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_ACCOUNTING_LIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ACCOUNTING_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ACCOUNTING_TRANSITS)>,
    // IP_ADDRESS_POOL – AddressPoolMode (enum)
    AtomicField<bool CONFIG_INDEX_ARG(IP_ADDRESS_POOL_DHCP)>,
    AtomicField<bool CONFIG_INDEX_ARG(IP_ADDRESS_POOL_DHCP_PROXY)>,
    AtomicField<bool CONFIG_INDEX_ARG(IP_ADDRESS_POOL_LOCAL)>,
    // IP_ARP_GRATUITOUS – GratuitousType (enum)
    AtomicField<int CONFIG_INDEX_ARG(Global::IP_ARP_GRATUITOUS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ARP_INCOMPLETE)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ARP_INCOMPLETE_ENTRIES)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ARP_INCOMPLETE_RETRY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ARP_PROXY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ARP_QUEUE)>,
    // IP_AS_PATH_ACCESS_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_AS_PATH_ACCESS_LIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_BGP_COMMUNITY_NEW_FORMAT)>,
    // IP_CEF – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_CEF)>,
    // IP_CLASSLESS – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_CLASSLESS)>,
    // IP_COMMUNITY_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_COMMUNITY_LIST)>,
    // IP_DEFAULT_NETWORK – ipv4
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_DEFAULT_NETWORK)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_DEFAULT_GATEWAY)>,
    // IP_DHCP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_DHCP)>,
    // IP_DHCP_CLIENT – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_DHCP_CLIENT)>,
    // IP_DHCP_RELAY – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_DHCP_RELAY)>,
    // IP_DHCP_SERVER – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_DHCP_SERVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_DOMAIN_LOOKUP_NSAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_DOMAIN_LOOKUP_RECURSIVE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::IP_DOMAIN_MULTICAST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_DOMAIN_RECURSIVE_ALLOW_SOA)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_DOMAIN_RECURSIVE_RETRY)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_DOMAIN_RETRY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_DOMAIN_ROUND_ROBIN)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_DOMAIN_TIMEOUT)>,
    // IP_EXPLICIT_PATH_IDENTIFIER – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::IP_EXPLICIT_PATH_IDENTIFIER)>,
    // IP_EXPLICIT_PATH_NAME – int reference
    OwnedListField<EmptyRegistry, int CONFIG_INDEX_ARG(Global::IP_EXPLICIT_PATH_NAME)>,
    // IP_EXTCOMMUNITY_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_EXTCOMMUNITY_LIST)>,
    // IP_FLOW_AGGREGATION_CACHE – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_FLOW_AGGREGATION_CACHE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_FLOW_CACHE_ENTRIES)>,
    // IP_FLOW_CACHE_MPLS – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_FLOW_CACHE_MPLS)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_FLOW_CACHE_TIMEOUT_ACTIVE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_FLOW_CACHE_TIMEOUT_INACTIVE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_FRAGMENT_OFFSET)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_ICMP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_IP_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_MAC)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_PACKET_LENGTH)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_TTL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_VLAN_ID)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_CAPTURE_EGRESS_INPUT_INTERFACE)>,
    // IP_FLOW_EXPORT_DESTINATION – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_DESTINATION)>,
    // IP_FLOW_EXPORT_SOURCE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_SOURCE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_TEMPLATE_OPTIONS_EXPORT_STATS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_TEMPLATE_OPTIONS_REFRESH_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_TEMPLATE_OPTIONA_TIMEOUT_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_TEMPLATE_REFRESH_RATE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_TEMPLATE_TIMEOUT_RATE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_VERSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_VERSION_BGP_NEXT_HOP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_VERSION_ORIGIN_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_EXPORT_VERSION_PEER_AS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_FLOW_TOP_TALKERS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_HOSTNAME_STRICT)>,
    // IP_HTTP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_HTTP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ICMP_RATE_LIMIT_UNREACHABLE_PER_MS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ICMP_RATE_LIMIT_UNREACHABLE_DF)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ICMP_RATE_LIMIT_UNREACHABLE_TRIGGER)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_ICMP_RATE_LIMIT_UNREACHABLE_LOG_PER_MS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ICMP_REDIRECT_HOST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ICMP_REDIRECT_SUBNET)>,
    // IP_KERBEROS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global::IP_KERBEROS_SOURCE_INTERFACE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::IP_LOCAL_POLICY_ROUTE_MAP)>,
    // IP_LOCAL_POOL – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_LOCAL_POOL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_MFIB)>,
    // IP_NAT – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_NAT)>,
    // IP_NBAR – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_NBAR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_OSPF_NAME_LOOKUP)>,
    // IP_POLICY_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_POLICY_LIST)>,
    // IP_PREFIX_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_PREFIX_LIST)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IP_REFLEXIVE_LIST_TIMEOUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_ROUTING)>,
    // IP_ROUTING_PROTOCOL_PURGE_INTERFACE – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_ROUTING_PROTOCOL_PURGE_INTERFACE)>,
    // IP_RSVP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_RSVP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_SCP_SERVER)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_SECURITY_ESO_INFO_SOURCE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_SECURITY_ESO_INFO_MAX_C_BYTES)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IP_SECURITY_ESO_INFO_DEFAULT_BIT)>,
    // IP_SLA – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_SLA)>,
    // IP_SSH – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_SSH)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_STICKY_ARP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IP_SUBNET_ZERO)>,
    // IP_TACACS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global::IP_TACACS_SOURCE_INTERFACE)>,
    // IP_TCP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_TCP)>,
    // IP_TELNET – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_TELNET)>,
    // IP_TFTP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IP_TFTP)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::IP_TRAFFIC_EXPORT_PROFILE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_VERIFY_DROP_RATE_COMPUTE_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_VERIFY_DROP_RATE_COMPUTE_WINDOW)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IP_VERIFY_DROP_RATE_NOTIFY_HOLD_DOWN)>,
    // IP_VRF – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::IP_VRF)>,
    // IPv6
    // IPV6_ACCESS_LIST – word reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::IPV6_ACCESS_LIST)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IPV6_ACCESS_LIST_LOG_UPDATE_THRESHOLD)>,
    // IPV6_ACCESS_LIST_ROLE_BASED – word reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::IPV6_ACCESS_LIST_ROLE_BASED)>,
    // IPV6_CEF – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_CEF)>,
    // IPV6_DHCP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_DHCP)>,
    // IPV6_DHCP_CLIENT – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_DHCP_CLIENT)>,
    // IPV6_DHCP_RELAY – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_DHCP_RELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IPV6_FLOWSET)>,
    // IPV6_GENERAL_PREFIX – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_GENERAL_PREFIX)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::IPV6_HOP_LIMIT)>,
    // IPV6_HOST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_HOST)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::IPV6_ICMP_ERROR_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IPV6_ICMP_BUCKET_SIZE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::IPV6_LOCAL_POLICY_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IPV6_MFIB)>,
    // IPV6_ND – reference
    ReferenceContainer<NdpBaseRegistry CONFIG_INDEX_ARG(Global::IPV6_ND)>,
    // IPV6_NEIGHBOR – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_NEIGHBOR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IPV6_OSPF_NAME_LOOKUP)>,
    // IPV6_PREFIX_LIST – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_PREFIX_LIST)>,
    // IPV6_PREFIX_POOL – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::IPV6_PREFIX_POOL)>,
    // IPV6_RADIUS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global::IPV6_RADIUS_SOURCE_INTERFACE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IPV6_SPD_QUEUE_MAX_THRESHOLD)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::IPV6_SPD_QUEUE_MIN_THRESHOLD)>,
    // IPV6_TACACS_SOURCE_INTERFACE – interfacekey
    OptionalValueField<interface::InterfaceKey CONFIG_INDEX_ARG(Global::IPV6_TACACS_SOURCE_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IPV6_TRAFFIC_INTERFACE_STATISTICS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::IPV6_TRAFFIC_INTERFACE_STATISTICS_UNCLEARABLE)>,
    // KERBEROS – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::KERBEROS)>,
    // KEY_CHAIN – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::KEY_CHAIN)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::KEY_CONFIG_KEY)>,
    // KRON – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::KRON)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::L2_PSEUDOWIRE_ROUTING)>,
    // L2_ROUTER_ID – ipv4
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global::L2_ROUTER_ID)>,
    // L2_VFI – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::L2_VFI)>,
    // L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::L2VPN_PSEUDOWIRE_STATIC_OAM_CLASS)>,
    // L2VPN_VFI_CONTEXT – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::L2VPN_VFI_CONTEXT)>,
    // L2VPN_XCONNECT_CONTEXT – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::L2VPN_XCONNECT_CONTEXT)>,
    // L3VPN_ENCAPSULATION_IP_PROFILE – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::L3VPN_ENCAPSULATION_IP_PROFILE)>,
    // LINE_RANGE
    OptionalValueField<std::pair<uint16_t, uint16_t> CONFIG_INDEX_ARG(Global::LINE_RANGE)>,
    // LINE_AUX, LINE_CONSOLE, LINE_VTY – uint16 references
    OwnedListField<EmptyRegistry, uint16_t CONFIG_INDEX_ARG(Global::LINE_AUX)>,
    OwnedListField<EmptyRegistry, uint16_t CONFIG_INDEX_ARG(Global::LINE_CONSOLE)>,
    OwnedListField<EmptyRegistry, uint16_t CONFIG_INDEX_ARG(Global::LINE_VTY)>,
    // LOGGING – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::LOGGING)>,
    // LOGIN
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::LOGIN_BLOCK_FOR_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::LOGIN_BLOCK_FOR_ATTEMPTS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::LOGIN_BLOCK_FOR_WITHIN)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::LOGIN_DELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::LOGIN_ON_FAILURE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::LOGIN_ON_FAILURE_LOG)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::LOGIN_ON_FAILURE_LOG_EVERY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::LOGIN_ON_SUCCESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::LOGIN_ON_SUCCESS_LOG)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::LOGIN_ON_SUCCESS_LOG_EVERY)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::LOGIN_QUITE_MODE_ACCESS_CLASS)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::LOGIN_STRING_NAME)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::LOGIN_STRING_LINE)>,
    // MLS
    AtomicField<bool CONFIG_INDEX_ARG(Global::MLS_RP_IP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::MLS_RP_IP_INPUT_ACL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::MLS_RP_IP_ROUTE_MAP)>,
    // MLS_RP_NDE_ADDRESS – ipv4
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global::MLS_RP_NDE_ADDRESS)>,
    // MONITOR_EVENT_TRACE – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::MONITOR_EVENT_TRACE)>,
    // MPLS – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::MPLS)>,
    // NETCONF
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::NETCONF_FORMAT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::NETCONF_LOCKTIME)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::NETCONF_MAX_MESSAGE)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::NETCONF_MAX_SESSIONS)>,
    // NTP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::NTP)>,
    // OBJECT_GROUP_SECURITY – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::OBJECT_GROUP_SECURITY)>,
    // PASSWORD
    AtomicField<bool CONFIG_INDEX_ARG(Global::PASSWORD_ENCRYPTION_AES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::PASSWORD_LOGGING)>,
    // POLICY_MAP – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::POLICY_MAP)>,
    // PRIVILEGED – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::PRIVILEGED)>,
    // QOS
    AtomicField<bool CONFIG_INDEX_ARG(Global::QOS_POLICE_ORDER_PARENT_FIRST)>,
    AtomicField<int CONFIG_INDEX_ARG(Global::QOS_SHAME_TIMER)>, // 1 or 4
    // ROUTE_MAP – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::ROUTE_MAP)>,
    // ROUTE_TAG_LIST – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::ROUTE_TAG_LIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::ROUTE_TAG_NOTATION_DOTTED_DECIMAL)>,
    // SAMPLER – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::SAMPLER)>,
    // SASL_PROFILE – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::SASL_PROFILE)>,
    // SCRIPTING
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::SCRIPTING_TCL_ENCDIR)>,
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::SCRIPTING_TCL_INIT)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Global::SCRIPTING_TCL_LOW_MEMORY)>,
    // SECURITY
    AtomicField<uint16_t CONFIG_INDEX_ARG(Global::SECURITY_AUTH_FAILURE_RATE_THRESHOLD)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::SECURITY_AUTH_FAILURE_RATE_THRESHOLD_LOG)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::SECURITY_PASSWORDS_MIN_LENGTH)>,
    // SERVICE – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::SERVICE)>,
    OptionalValueField<std::string CONFIG_INDEX_ARG(Global::SERVICE_POLICY_TYPE_CONTROL)>,
    // SNMP
    AtomicField<bool CONFIG_INDEX_ARG(Global::SNMP_IFMIB_IFALIAS_LONG)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::SNMP_IFMIB_IFINDEX_PERSIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::SNMP_IFMIB_TRAP_THROTTLE)>,
    // SNMP_MIB – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::SNMP_MIB)>,
    // SNMP_SERVER – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::SNMP_SERVER)>,
    // STANDBY
    AtomicField<bool CONFIG_INDEX_ARG(Global::STANDBY_BFD_ALL_INTERFACES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::STANDBY_REDIRECTS)>,
    // TACACS_SERVER – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::TACACS_SERVER)>,
    // TIME_RANGE – string reference
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::TIME_RANGE)>,
    // TRACK_OBJECT – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::TRACK_OBJECT)>,
    // TRACK_RESOLUTION
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::TRACK_RESOLUTION_IP_ROUTE_BGP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::TRACK_RESOLUTION_IP_ROUTE_EIGRP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::TRACK_RESOLUTION_IP_ROUTE_OSPF)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Global::TRACK_RESOLUTION_IP_ROUTE_STATIC)>,
    // USERNAME – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::USERNAME)>,
    // VRF
    OwnedListField<VrfRegistry, std::string CONFIG_INDEX_ARG(Global::VRF_CONFIGS)>,
    OwnedListField<EmptyRegistry, std::string CONFIG_INDEX_ARG(Global::VRF_LIST)>,
    // VRF_SELECTION – incomplete
    ValueField<Incomplete CONFIG_INDEX_ARG(Global::VRF_SELECTION)>,
    // WARM REBOOT
    AtomicField<bool CONFIG_INDEX_ARG(Global::WARM_REBOOT)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::WARM_REBOOT_COUNT)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Global::WARM_REBOOT_UPTIME)>,
    // XCONNECT
    AtomicField<bool CONFIG_INDEX_ARG(Global::XCONNECT_LOGGING_PSEUDOWIRE_STATUS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Global::XCONNECT_LOGGING_REDUNDANCY)>
>;
}

#endif // GLOBAL_REGISTRY_HPP
