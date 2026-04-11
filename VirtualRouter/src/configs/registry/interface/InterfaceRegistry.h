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
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

#include "ArpRegistry.h"
#include "NdpRegistry.h"

#undef IP_MTU
#undef IPV6_MTU

struct IncompleteIf {};
enum class EmptyIf { COUNT };
using EmptyRegistryIf = config::SubRegistry<EmptyIf>;

namespace interface { struct InterfaceKey; }

namespace config
{

enum class InterfaceBase
{
};

enum class Interface
{
    AAA_CONNECTION_INFO, // TODO string
    ARP,
    BANDWIDTH, // TODO uint32
    BANDWIDTH_RECEIVE, // TODO uint32
    BANDWIDTH_INHERITANCE, // TODO uint32
    BANDWIDTH_RECEIVE_INHERITANCE, // TODO uint32
    BFD_ECHO, // TODO bool
    BFD_INTERVAL, // TODO uint16
    BFD_INTERVAL_MIN_RX, // TODO uint16
    BFD_INTERVAL_MULTIPLIER, // TODO uint8
    BFD_TEMPLATE, // TODO string
    CDP, // TODO bool
    CHANNEL_GROUP, // TODO optional uint8
    CRYPTO, // TODO
    DAMPENING_HALF_LIFE, // TODO uint8
    DAMPENING_REUSE, // TODO uint16
    DAMPENING_THRESHOLD, // TODO uint16
    DAMPENING_MAX_DURATION, // TODO uint8
    DAMPENING_RESTART_PENALTY, // TODO optional uint16
    DELAY, // TODO uint32
    DESCRIPTION, // TODO string
    DOT1Q_TUNNELING_ETHERNET, // TODO uint16
    FLOW_SAMPLER, // TODO
    GLBP, // TODO
    HISTORY_BPS, // TODO
    HISTORY_PPS, // TODO
    HOLD_QUEUE_LENGTH, // TODO //uint32
    IP_ACCESS_GROUP,
    IP_ADDRESS, // TODO list of ipprefix
    IP_ADDRESS_SECONDARY, // TODO IPPrefix, string
    IP_ADDRESS_DHCP, // TODO
    IP_BFD_FAST_EXTERNAL_FALLOVER, // TODO bool
    IP_CEF_ACCOUNTING_NON_RECURSIVE, // TODO bool
    IP_DHCP, // TODO
    IP_EIGRP, // TODO
    IP_DIRECT_BROADCAST, // TODO
    IP_FLOW_INGRESS, // TODO bool
    IP_FLOW_EGRESS, // TODO bool
    IP_FLOW_MONITOR, // TODO
    IP_HELPER_ADDRESS, // TODO IPAddress, bool, string
    IP_IGMP, // TODO
    IP_LISP_SOURCE_LOCATOR, // TODO interface::interfacekey
    IP_LOAD_SHARING_PER_DESTINATION, // TODO bool
    IP_LOAD_SHARING_PER_PACKET, // TODO bool
    IP_LOCAL_PROXY_ARP, // TODO bool
    IP_MASK_REPLY, // TODO bool
    IP_MFIB_CEF_INPUT, // TODO bool
    IP_MFIB_CEF_OUTPUT, // TODO bool
    IP_MFIB_FORWARDING_INPUT, // TODO bool
    IP_MFIB_FORWARDING_OUTPUT, // TODO bool
    IP_MTU, // TODO, uint16
    IP_MULTICAST, // TODO
    IP_NAT_INSIDE, // TODO bool
    IP_NAT_OUTSIDE, // TODO bool
    IP_NBAR_PROTOCOL_DISCOVER_IP, // TODO bool
    IP_NBAR_PROTOCOL_DISCOVER_IPV6, // TODO bool
    IP_OSPF, // TODO
    IP_PIM, // TODO
    IP_POLICY_ROUTE_MAP, // TODO string
    IP_PROXY_ARP, // TODO bool
    IP_REDIRECTS, // TODO bool
    IP_RIP, // TODO
    IP_ROUTE_CACHE, // TODO bool
    IP_ROUTE_CACHE_CEF, // TODO bool
    IP_ROUTE_CACHE_FLOW, // TODO bool
    IP_ROUTE_CACHE_POLICY, // TODO bool
    IP_ROUTE_CACHE_SAME_INTERFACE, // TODO bool
    IP_RSVP, // TODO
    IP_RTP_COMPRESSION_CONNECTIONS, //TODO uint16
    IP_RTP_HEADER_COMPRESSION, // TODO
    IP_TCP_ADJUST_MSS, // TODO uint16
    IP_TCP_COMPRESSION_CONNECTIONS, // TODO uint16
    IP_TCP_HEADER_FORMAT, // TODO
    IP_TRAFFIC_EXPORT_APPLY, // TODO string
    IP_VERIFY_UNICAST_NOTIFICATION_DROP_RATE, // TODO uint32
    IP_VERIFY_UNICAST_REVERSE_PATH, // TODO
    IP_VERIFY_UNICAST_REVERSE_PATH_ALLOW_SELF_PING, // TODO
    IP_VERIFY_UNICAST_SOURCE_REACHABLE_VIA, // TODO
    IP_VRF, // TODO
    IPV6_ADDRESS_LL, // TODO IPv6prefix
    IPV6_ADDRESS, // TODO
    IPV6_ADDRESS_AUTOCONFIG, // TODO bool
    IPV6_ADDRESS_AUTOCONFIG_DEFAULT, // TODO bool
    IPV6_ADDRESS_DHCP, // TODO 
    IPV6_CEF, // TODO bool
    IPV6_DHCP, // TODO
    IPV6_EIGRP, // TODO as list
    IPV6_EIGRP_ENABLED,
    IPV6_FLOW_MONITOR, // TODO bool
    IPV6_LIST_SOURCE_LOCATOR, // TODO interface::InterfaceKEy
    IPV6_MFIB_CEF_INPUT, // TODO bool
    IPV6_MFIB_CEF_OUTPUT, // TODO bool
    IPV6_MFIB_FORWARDING_INPUT, // TODO bool
    IPV6_MFIB_FORWARDING_OUTPUT, // TODO bool
    IPV6_MFIB_FORWARDING, // TODO bool
    IPV6_MTU, // TODO, uint16
    IPV6_MULTICAST, // TODO
    IPV6_ND,
    IPV6_OSPF,
    IPV6_PIM, // TODO
    IPV6_POLICY_ROUTE_MAP, // TODO string
    IPV6_REDIRECTS, // TODO bool
    IPV6_RIP, // TODO
    IPV6_TRAFFIC_FILTER_IN, // TODO string
    IPV6_TRAFFIC_FILTER_OUT, // TODO string
    IPV6_VERIFY_UNICAST_REVERSE_PATH, // TODO bool
    IPV6_VERIFY_UNICAST_REVERSE_PATH_ACCESS_LIST, // TODO
    IPV6_VERIFY_UNICAST_SOURCE_REACHABLE_VIA, // TODO
    KEEPALIVE, // TODO bool
    KEEPALIVE_PERIOD, // TODO uint32
    LAN_NAME, // TODO string
    LOAD_INTERVAL, // TODO uint16
    LOOPBACK_DRIVER, // TODO bool
    LOOPBACK_MAC, // TODO bool
    MAC_ADDRESS, // TODO mac
    MPLS, // TODO
    MTU, // TODO uint16
    NEGOTIATION_AUTO, // TODO bool
    NTP, // TODO
    OSPFV3_DEFAULT, // TODO ref container
    OSPFV3, // TODO uint16 reference
    RATE_LIMIT, // TODO 
    RMON, // TODO
    SERVICE_POLICY_INPUT, // TODO string
    SERVICE_POLICY_OUTPUT, // TODO string
    SERVICE_POLICY_CONTROL, // TODO string
    SERVICE_POLICY_CONTROL_DEFAULT, // TODO bool
    SHUTDOWN, // TODO
    SNMP_IFINDEX_PERSIST, // TODO bool
    SNMP_TRAP_IP_VERIFY_DROP_RATE, // TODO bool
    SNMP_TRAP_LINK_STATUS, // TODO bool
    SNMP_TRAP_LINK_STATUS_PERMIT_DUPLICATES, // TODO bool
    STANDBY, // TODO
    TIMEOUT_ABSOLUTE_LIFETIME, // TODO uint32
    VLAN_ID_DOT1Q, // TODO uint16 reference
    VRF_FORWARDING, // TODO // string
    VRRP, // TODO
    COUNT
};

#define INTERFACE_DEFAULTS(X) \
    (Interface, BANDWIDTH, 0) \
    (Interface, BANDWIDTH_RECEIVE, 0) \
    (Interface, BFD_ECHO, true) \
    (Interface, BFD_INTERVAL, 250) \
    (Interface, BFD_INTERVAL_MIN_RX, 250) \
    (Interface, BFD_INTERVAL_MULTIPLIER, 3) \
    (Interface, CDP, true) \
    (Interface, DAMPENING_HALF_LIFE, 5) \
    (Interface, DAMPENING_REUSE, 1000) \
    (Interface, DAMPENING_THRESHOLD, 2000) \
    (Interface, DAMPENING_MAX_DURATION, 20) \
    (Interface, DAMPENING_RESTART_PENALTY, 1000) \
    (Interface, DELAY, 0) \
    (Interface, DOT1Q_TUNNELING_ETHERNET, 0x8100) \
    (Interface, HOLD_QUEUE_LENGTH, 40) \
    (Interface, IP_ADDRESS_DHCP, false) \
    (Interface, IP_BFD_FAST_EXTERNAL_FALLOVER, false) \
    (Interface, IP_CEF_ACCOUNTING_NON_RECURSIVE, false) \
    (Interface, IP_FLOW_INGRESS, false) \
    (Interface, IP_FLOW_EGRESS, false) \
    (Interface, IP_LOAD_SHARING_PER_DESTINATION, true) \
    (Interface, IP_LOAD_SHARING_PER_PACKET, false) \
    (Interface, IP_LOCAL_PROXY_ARP, false) \
    (Interface, IP_MASK_REPLY, false) \
    (Interface, IP_MFIB_CEF_INPUT, true) \
    (Interface, IP_MFIB_CEF_OUTPUT, true) \
    (Interface, IP_MFIB_FORWARDING_INPUT, false) \
    (Interface, IP_MFIB_FORWARDING_OUTPUT, false) \
    (Interface, IP_MTU, 1500) \
    (Interface, IP_NAT_INSIDE, false) \
    (Interface, IP_NAT_OUTSIDE, false) \
    (Interface, IP_NBAR_PROTOCOL_DISCOVER_IP, false) \
    (Interface, IP_NBAR_PROTOCOL_DISCOVER_IPV6, false) \
    (Interface, IP_PROXY_ARP, true) \
    (Interface, IP_REDIRECTS, true) \
    (Interface, IP_ROUTE_CACHE, true) \
    (Interface, IP_ROUTE_CACHE_CEF, true) \
    (Interface, IP_ROUTE_CACHE_FLOW, false) \
    (Interface, IP_ROUTE_CACHE_POLICY, false) \
    (Interface, IP_ROUTE_CACHE_SAME_INTERFACE, false) \
    (Interface, IP_RTP_COMPRESSION_CONNECTIONS, 16) \
    (Interface, IP_TCP_ADJUST_MSS, 0) \
    (Interface, IP_TCP_COMPRESSION_CONNECTIONS, 16) \
    (Interface, IP_VERIFY_UNICAST_NOTIFICATION_DROP_RATE, 0) \
    (Interface, IPV6_ADDRESS_AUTOCONFIG, false) \
    (Interface, IPV6_ADDRESS_AUTOCONFIG_DEFAULT, false) \
    (Interface, IPV6_CEF, true) \
    (Interface, IPV6_FLOW_MONITOR, false) \
    (Interface, IPV6_MFIB_CEF_INPUT, true) \
    (Interface, IPV6_MFIB_CEF_OUTPUT, true) \
    (Interface, IPV6_MFIB_FORWARDING_INPUT, false) \
    (Interface, IPV6_MFIB_FORWARDING_OUTPUT, false) \
    (Interface, IPV6_MFIB_FORWARDING, false) \
    (Interface, IPV6_MTU, 1500) \
    (Interface, IPV6_REDIRECTS, true) \
    (Interface, IPV6_VERIFY_UNICAST_REVERSE_PATH, false) \
    (Interface, KEEPALIVE, true) \
    (Interface, KEEPALIVE_PERIOD, 10) \
    (Interface, LOAD_INTERVAL, 300) \
    (Interface, LOOPBACK_DRIVER, false) \
    (Interface, LOOPBACK_MAC, false) \
    (Interface, MTU, 1500) \
    (Interface, NEGOTIATION_AUTO, true) \
    (Interface, SERVICE_POLICY_CONTROL_DEFAULT, false) \
    (interface, SHUTDOWN, true) \
    (Interface, SNMP_IFINDEX_PERSIST, false) \
    (Interface, SNMP_TRAP_IP_VERIFY_DROP_RATE, false) \
    (Interface, SNMP_TRAP_LINK_STATUS, true) \
    (Interface, SNMP_TRAP_LINK_STATUS_PERMIT_DUPLICATES, false) \
    (Interface, TIMEOUT_ABSOLUTE_LIFETIME, 0)

void interfaceIPAddress(void*);
void interfaceIPAddressSecondary(void*);
void interfaceShutdown(void*);
void interfaceIPv6Eigrp(void*);

using InterfaceRegistry = SubRegistry<Interface,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::AAA_CONNECTION_INFO)>,
    RegistryContainer<ArpRegistry CONFIG_INDEX_ARG(Interface::ARP)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::BANDWIDTH)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::BANDWIDTH_RECEIVE)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Interface::BANDWIDTH_INHERITANCE)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Interface::BANDWIDTH_RECEIVE_INHERITANCE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::BFD_ECHO)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::BFD_INTERVAL)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::BFD_INTERVAL_MIN_RX)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Interface::BFD_INTERVAL_MULTIPLIER)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::BFD_TEMPLATE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::CDP)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Interface::CHANNEL_GROUP)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::CRYPTO)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Interface::DAMPENING_HALF_LIFE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::DAMPENING_REUSE)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::DAMPENING_THRESHOLD)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Interface::DAMPENING_MAX_DURATION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::DAMPENING_RESTART_PENALTY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::DELAY)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::DESCRIPTION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::DOT1Q_TUNNELING_ETHERNET)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::FLOW_SAMPLER)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::GLBP)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::HISTORY_BPS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::HISTORY_PPS)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::HOLD_QUEUE_LENGTH)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_ACCESS_GROUP)>,
    OptionalAtomicField<types::IPv4Prefix CONFIG_INDEX_ARG(Interface::IP_ADDRESS), interfaceIPAddress>,
    ListField<std::tuple<types::IPv4Prefix, std::string> CONFIG_INDEX_ARG(Interface::IP_ADDRESS_SECONDARY), interfaceIPAddressSecondary>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ADDRESS_DHCP), interfaceIPAddress>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_BFD_FAST_EXTERNAL_FALLOVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_CEF_ACCOUNTING_NON_RECURSIVE)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_DHCP)>,
    OwnedListField<EigrpInterfaceRegistry, uint16_t CONFIG_INDEX_ARG(Interface::IP_EIGRP)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_DIRECT_BROADCAST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_FLOW_INGRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_FLOW_EGRESS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_FLOW_MONITOR)>,
    ListField<std::tuple<types::IPv4Address, bool, std::string> CONFIG_INDEX_ARG(Interface::IP_HELPER_ADDRESS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_IGMP)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(Interface::IP_LISP_SOURCE_LOCATOR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_LOAD_SHARING_PER_DESTINATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_LOAD_SHARING_PER_PACKET)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_LOCAL_PROXY_ARP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_MASK_REPLY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_MFIB_CEF_INPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_MFIB_CEF_OUTPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_MFIB_FORWARDING_INPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_MFIB_FORWARDING_OUTPUT)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::IP_MTU)>,
    AtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_MULTICAST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_NAT_INSIDE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_NAT_OUTSIDE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_NBAR_PROTOCOL_DISCOVER_IP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_NBAR_PROTOCOL_DISCOVER_IPV6)>,
    RegistryContainer<OspfInterfaceBaseRegistry CONFIG_INDEX_ARG(Interface::IP_OSPF)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_PIM)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::IP_POLICY_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_PROXY_ARP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_REDIRECTS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_RIP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ROUTE_CACHE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ROUTE_CACHE_CEF)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ROUTE_CACHE_FLOW)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ROUTE_CACHE_POLICY)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IP_ROUTE_CACHE_SAME_INTERFACE)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_RSVP)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::IP_RTP_COMPRESSION_CONNECTIONS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_RTP_HEADER_COMPRESSION)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::IP_TCP_ADJUST_MSS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::IP_TCP_COMPRESSION_CONNECTIONS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_TCP_HEADER_FORMAT)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::IP_TRAFFIC_EXPORT_APPLY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::IP_VERIFY_UNICAST_NOTIFICATION_DROP_RATE)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_VERIFY_UNICAST_REVERSE_PATH)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_VERIFY_UNICAST_REVERSE_PATH_ALLOW_SELF_PING)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_VERIFY_UNICAST_SOURCE_REACHABLE_VIA)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IP_VRF)>,
    OptionalAtomicField<types::IPv6Address CONFIG_INDEX_ARG(Interface::IPV6_ADDRESS_LL)>,
    ListField<std::tuple<types::IPv6Address, std::string, IGNOR(bool), IGNOR(bool)> CONFIG_INDEX_ARG(Interface::IPV6_ADDRESS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_ADDRESS_AUTOCONFIG)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_ADDRESS_AUTOCONFIG_DEFAULT)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_ADDRESS_DHCP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_CEF)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_DHCP)>,
    OwnedListField<EigrpInterfaceRegistry, uint16_t CONFIG_INDEX_ARG(Interface::IPV6_EIGRP)>,
    ListField<uint16_t CONFIG_INDEX_ARG(Interface::IPV6_EIGRP_ENABLED), interfaceIPv6Eigrp>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_FLOW_MONITOR)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(Interface::IPV6_LIST_SOURCE_LOCATOR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_MFIB_CEF_INPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_MFIB_CEF_OUTPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_MFIB_FORWARDING_INPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_MFIB_FORWARDING_OUTPUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_MFIB_FORWARDING)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::IPV6_MTU)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_MULTICAST)>,
    RegistryContainer<NdpRegistry CONFIG_INDEX_ARG(Interface::Interface::IPV6_ND)>,
    RegistryContainer<OspfInterfaceBaseRegistry CONFIG_INDEX_ARG(Interface::IPV6_OSPF)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_PIM)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::IPV6_POLICY_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_REDIRECTS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_RIP)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::IPV6_TRAFFIC_FILTER_IN)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::IPV6_TRAFFIC_FILTER_OUT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::IPV6_VERIFY_UNICAST_REVERSE_PATH)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_VERIFY_UNICAST_REVERSE_PATH_ACCESS_LIST)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::IPV6_VERIFY_UNICAST_SOURCE_REACHABLE_VIA)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::KEEPALIVE)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::KEEPALIVE_PERIOD)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::LAN_NAME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::LOAD_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::LOOPBACK_DRIVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::LOOPBACK_MAC)>,
    OptionalAtomicField<types::Mac CONFIG_INDEX_ARG(Interface::MAC_ADDRESS)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::MPLS)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Interface::MTU)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::NEGOTIATION_AUTO)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::NTP)>,
    RegistryContainer<OspfInterfaceBaseRegistry CONFIG_INDEX_ARG(Interface::OSPFV3_DEFAULT)>,
    OwnedListField<OspfInterfaceRegistry, uint16_t CONFIG_INDEX_ARG(Interface::OSPFV3)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::RATE_LIMIT)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::RMON)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::SERVICE_POLICY_INPUT)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::SERVICE_POLICY_OUTPUT)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::SERVICE_POLICY_CONTROL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::SERVICE_POLICY_CONTROL_DEFAULT)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::SHUTDOWN), interfaceShutdown>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::SNMP_IFINDEX_PERSIST)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::SNMP_TRAP_IP_VERIFY_DROP_RATE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::SNMP_TRAP_LINK_STATUS)>,
    AtomicField<bool CONFIG_INDEX_ARG(Interface::SNMP_TRAP_LINK_STATUS_PERMIT_DUPLICATES)>,
    ValueField<IncompleteIf CONFIG_INDEX_ARG(Interface::STANDBY)>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Interface::TIMEOUT_ABSOLUTE_LIFETIME)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::VLAN_ID_DOT1Q)>,
    ValueField<std::string CONFIG_INDEX_ARG(Interface::VRF_FORWARDING)>,
    OptionalAtomicField<IncompleteIf CONFIG_INDEX_ARG(Interface::VRRP)>
>;
}

#endif // INTERFACE_REGISTRY_H
