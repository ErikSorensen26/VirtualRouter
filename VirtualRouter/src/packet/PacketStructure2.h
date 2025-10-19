// PacketStructure.h

#ifndef PACKET_STRUCTURE_H
#define PACKET_STRUCTURE_H

#include <cstdint>
#include <cstddef>

#include "EthernetHeader.hpp"
#include "ArpHeader.hpp"
#include "MplsHeader.hpp"
#include "IPv4Header.hpp"
#include "IPv6Header.hpp"
#include "TcpHeader.hpp"
#include "UdpHeader.hpp"
#include "IcmpHeader.hpp"
#include "Icmpv6Header.hpp"
#include "AhHeader.hpp"
#include "EspHeader.hpp"
#include "DhcpHeader.hpp"
#include "Dhcpv6Header.hpp"
#include "Dhcpv6RelayHeader.hpp"
#include "EigrpHeader.hpp"
#include "SyslogHeader.hpp"

// EtherTypes
#define ETHERNET_ARP    0x0806      ///< EtherType for ARP
#define ETHERNET_IPV4   0x0800      ///< EtherType for IPv4
#define ETHERNET_IPV6   0x86DD      ///< EtherType for IPv6
#define ETHERNET_MPLS   0x8847      ///< EtherType for MPLS
#define ETHERNET_VLAN   0x8100      ///< EtherType for VLAN
#define ETHERNET_LLDP   0x88CC      ///< EtherType for LLDP

// Arp
#define ARP_HARDWARE_ETHERNET       0x0001      ///< ARP hardware type for Ethernet
#define ARP_HARDWARE_IPV4           0x0800      ///< ARP hardware type for IPv4

#define ARP_OPCODE_REQUEST          0x0001      ///< ARP Request
#define ARP_OPCODE_REPLY            0x0002      ///< ARP Reply
#define ARP_OPCODE_REVERSE_REQUEST  0x0003      ///< Reverse ARP Request
#define ARP_OPCODE_REVERSE_REPLY    0x0004      ///< Reverse ARP Reply
#define ARP_OPCODE_DYNAMIC_REQUEST  0x0005      ///< Dynamic ARP Request
#define ARP_OPCODE_DYNAMIC_REPLY    0x0006      ///< Dynamic ARP Reply
#define ARP_OPCODE_DYNAMIC_ERROR    0x0007      ///< Dynamic ARP Error
#define ARP_OPCODE_INVERSE_REQUEST  0x0008      ///< Inverse ARP Error
#define ARP_OPCODE_INVERSE_REPLY    0x0009      ///< Inverse ARP Reply
#define ARP_OPCODE_NAK              0x000A      ///< ARP NAK (Negative Acknowledgment)

// ICMPv6
#define ICMPV6_OPCODE_UNREACHABLE                       0x01    ///< Unreachable error code for ICMPv6
#define ICMPV6_OPCODE_PACKET_TOO_BIG                    0x02    ///< Packet too big error for ICMPv6
#define ICMPV6_OPCODE_TIME_EXCEEDED                     0x03    ///< Time exceeded error for ICMPv6
#define ICMPV6_OPCODE_PARAMETER_PROBLEM                 0x04    ///< Parameter problem for ICMPv6
#define ICMPV6_OPCODE_ECHO_REQUEST                      0x80    ///< Echo request for Ping (128)
#define ICMPV6_OPCODE_ECHO_REPLY                        0x81    ///< Echo reply for Ping (129)
#define ICMPV6_OPCODE_MLD_LISTEN_QUERY                  0x82    ///< MLD Listen Query (130)
#define ICMPV6_OPCODE_MLD_LISTEN_REPORT                 0x83    ///< MLD Listen Report (131)
#define ICMPV6_OPCODE_MLD_LISTEN_DONE                   0x84    ///< MLD Listen Done (132)
#define ICMPV6_OPCODE_MLD_LISTEN_REPORT_V2              0x8F    ///< MLD Listen Report V2 (143)
#define ICMPV6_OPCODE_NDP_ROUTE_SOLICITATION            0x85    ///< NDP Route Solicitation (133)
#define ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT           0x86    ///< NDP Route Solicitation (134)
#define ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION         0x87    ///< NDP Neighbor Solicitation (135)
#define ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT        0x88    ///< NDP Neighbor Solicitation (136)
#define ICMPV6_OPCODE_NDP_REDIRECT_MESSAGE              0x89    ///< NDP Message Redirect (137)
#define ICMPV6_OPCODE_NODE_INFORMATION_QUERY            0x8B    ///< Node Information Query (139)
#define ICMPV6_OPCODE_NODE_INFORMATION_RESPONSE         0x8C    ///< Node Information Response (140)
#define ICMPV6_OPCODE_MULTICAST_ROUTER_ADVERTISEMENT    0x98    ///< Multicast Router Advertisement (152)
#define ICMPV6_OPCODE_MULTICAST_ROUTER_SOLICITATION     0x99    ///< Multicast Router Solicitation (153)
#define ICMPV6_OPCODE_MULTICAST_ROUTER_TERMINATION      0x9A    ///< Multicast Router Termination (154)
#define ICMPV6_OPCODE_EXTENDED_ECHO_REQUEST             0xA0    ///< Extended Echo Request (160)
#define ICMPV6_OPCODE_EXTENDED_ECHO_REPLY               0xA1    ///< Extended Echo Reply (161)

#define ICMPV6_OPTION_NDP_SOURCE        0x01    ///< Source Option for NDP
#define ICMPV6_OPTION_NDP_TARGET        0x02    ///< Target Option for NDP
#define ICMPV6_OPTION_NDP_PREFIX        0x03    ///< Prefix Information Option for NDP
#define ICMPV6_OPTION_NDP_REDIRECT      0x04    ///< Redirect Option for NDP
#define ICMPV6_OPTION_NDP_MTU           0x05    ///< MTU Option for NDP
#define ICMPV6_OPTION_NDP_NBMA          0x06    ///< NBMA Option for NDP
#define ICMPV6_OPTION_NDP_CGA           0x0B    ///< CGA Option for NDP
#define ICMPV6_OPTION_NDP_RSA           0x0C    ///< RSA Option for NDP
#define ICMPV6_OPTION_NDP_TIMESTAMP     0x0D    ///< Timestamp Option for NDP
#define ICMPV6_OPTION_NDP_NONCE         0x0E    ///< Nonce Option for NDP
#define ICMPV6_OPTION_NDP_TRUST_ANCHOR  0x0F    ///< Trust Anchor Option for NDP
#define ICMPV6_OPTION_NDP_CERTIFICATE   0x10    ///< Certification Option for NDP
#define ICMPV6_OPTION_NDP_ROUTE_INFO    0x18    ///< Route Information Option for NDP
#define ICMPV6_OPTION_NDP_DNS_SERVER    0x19    ///< DNS Server Option for NDP
#define ICMPV6_OPTION_NDP_DNS_SEARCH    0x1F    ///< DNS Search Option for NDP


#endif // PACKET_STRUCTURE_H
