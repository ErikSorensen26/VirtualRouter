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

// --- uint16_t ---
constexpr uint16_t NET16(uint16_t val) {
    return is_little_endian
        ? static_cast<uint16_t>((val << 8) | (val >> 8))
        : val;
}

// --- uint32_t ---
constexpr uint32_t NET32(uint32_t val) {
    return is_little_endian
        ? ((val & 0x000000FFU) << 24) |
          ((val & 0x0000FF00U) << 8) |
          ((val & 0x00FF0000U) >> 8) |
          ((val & 0xFF000000U) >> 24)
        : val;
}

static constexpr int MaxHeaders = 16;

/**
 * @file Encapsulation.h
 * @brief Defines constants, structures, and utility functions for packet encapsulation.
 */
namespace Variable
{
    /**
     * @namespace Ethernet
     * @brief Contains Ethernet-related EtherType constants.
     */
    namespace Ethernet
    {
        inline constexpr uint16_t arp = uint16_t{0x0806U};    ///< EtherType for ARP
        inline constexpr uint16_t ipv4 = uint16_t{0x0800U};   ///< EtherType for IPv4
        inline constexpr uint16_t ipv6 = uint16_t{0x86ddU};   ///< EtherType for IPv6
        inline constexpr uint16_t mpls = uint16_t{0x8847U};   ///< EtherType for MPLS
        inline constexpr uint16_t vlan = uint16_t{0x8100U};   ///< EtherType fr VLAN
        inline constexpr uint16_t lldp = uint16_t{0x88ccU};   ///< EtherType for LLDP
    }
    
    /**
     * @namespace Arp
     * @brief Contains ARP-related constants.
     */
    namespace Arp
    {
        inline constexpr uint16_t ethernet = uint16_t{0x0001U};   ///< Hardware type for Ethernet
        inline constexpr uint16_t ipv4 = uint16_t{0x0800U};       ///< Hardware type for IPv4

        /**
         * @namespace Opcode
         * @brief Contains ARP opcode constants.
         */
        namespace Opcode
        {
            inline constexpr uint16_t request = uint16_t{0x0001U};           ///< ARP Request.
            inline constexpr uint16_t reply = uint16_t{0x0002U};             ///< ARP Reply.
            inline constexpr uint16_t reverseRequest = uint16_t{0x0003U};    ///< Reverse ARP Request.
            inline constexpr uint16_t reverseReply = uint16_t{0x0004U};      ///< Reverse ARP Reply.
            inline constexpr uint16_t dynamicRequest = uint16_t{0x0005U};    ///< Dynamic ARP Request.
            inline constexpr uint16_t dynamicReply = uint16_t{0x0006U};      ///< Dynamic ARP Reply.
            inline constexpr uint16_t dynamicError = uint16_t{0x0007U};      ///< Dynamic ARP Error.
            inline constexpr uint16_t inverseRequest = uint16_t{0x0008U};    ///< Inverse ARP Request.
            inline constexpr uint16_t inverseReply = uint16_t{0x0009U};      ///< Inverse ARP Reply.
            inline constexpr uint16_t nak = uint16_t{0x000aU};               ///< ARP NAK (Negative Acknowledgment).
        }
    }

    /**
     * @namespace ICMPv6
     * @brief Contains ICMPv6-related constants.
     */
    namespace ICMPv6
    {
        /**
         * @namespace Types
         * @brief Contains ICMPv6-related Type Constants.
         */
        namespace Type
        {
            inline constexpr uint8_t ureachable = uint8_t{0x01U};                         ///< Unreachable error code for ICMPv6.
            inline constexpr uint8_t packetTooBig = uint8_t{0x02U};                       ///< Packet Too Big error for ICMPv6.
            inline constexpr uint8_t timeExceeded = uint8_t{0x03U};                       ///< Time Exceeded error for ICMPv6.
            inline constexpr uint8_t parameterProblem = uint8_t{0x04U};                   ///< Parameter Problem for ICMPv6.

            inline constexpr uint8_t echoRequest = uint8_t{0x80U};                        ///< Echo request for Ping (128).
            inline constexpr uint8_t echoReply = uint8_t{0x81U};                          ///< Echo reply for Pint (129).

            inline constexpr uint8_t mldListenQuery = uint8_t{0x82U};                     ///< MLD Listen Query (130).
            inline constexpr uint8_t mldListenReport = uint8_t{0x83U};                    ///< MLD Listen Report (131).
            inline constexpr uint8_t mldListenDone = uint8_t{0x84U};                      ///< MLD Listen Done (132).
            inline constexpr uint8_t mldListenReportV2 = uint8_t{0x8FU};                  ///< MDL Listen Report V2 (143).

            inline constexpr uint8_t ndpRouteSolicitation = uint8_t{0x85U};               ///< NDP Route Solicitation (133).
            inline constexpr uint8_t ndpRouteAdvertisement = uint8_t{0x86U};              ///< NDP Route Advertisement (134).
            inline constexpr uint8_t ndpNeighborSolicitation = uint8_t{0x87U};            ///< NDP Neighbor Solicitation (135).
            inline constexpr uint8_t ndpNeighborAdvertisement = uint8_t{0x88U};           ///< NDP Neighbor Advertisement (136).
            inline constexpr uint8_t ndpRedirectMessage = uint8_t{0x89U};                 ///< NDP Message Redirect (137).

            inline constexpr uint8_t nodeInformationQuery = uint8_t{0x8BU};               ///< Node Information Query (139).
            inline constexpr uint8_t nodeInformationResponse = uint8_t{0x8CU};            ///< Node Information Response (140).

            inline constexpr uint8_t routerRenumbering = uint8_t{0x8DU};                  ///< Router Renumbering (141).

            inline constexpr uint8_t nodeInfoQuery = uint8_t{0x8BU};                      ///< Node Information Query (139).
            inline constexpr uint8_t nodeInfoResponse = uint8_t{0x8CU};                   ///< Node Information Response (140).

            inline constexpr uint8_t homeAgentAddressDiscoveryRequest = uint8_t{0x90U};   ///< Home Agent Address Discovery Request (144).
            inline constexpr uint8_t homeAgentAddressDiscoveryReply = uint8_t{0x91U};     ///< Home Agent Address Discovery Reply (145).
            inline constexpr uint8_t mobilePrefixSolicitation = uint8_t{0x92U};           ///< Mobile Prefix Solicitation (146).
            inline constexpr uint8_t mobilePrefixAdvertisement = uint8_t{0x93U};          ///< Mobile Prefix Advertisement (147).

            inline constexpr uint8_t certificationPathSolicitation = uint8_t{0x94U};      ///< Certification Path Solicitation (148).
            inline constexpr uint8_t certificationPathAdvertisement = uint8_t{0x95U};     ///< Certification Path Advertisement (149).

            inline constexpr uint8_t icmpExperiment1 = uint8_t{0x96U};                    ///< ICMP Experimentation (150).
            inline constexpr uint8_t icmpExperiment2 = uint8_t{0x97U};                    ///< ICMP Experimentation (151).

            inline constexpr uint8_t multicastRouterAdvertisement = uint8_t{0x98U};       ///< Multicast Router Advertisement (152).
            inline constexpr uint8_t multicastRouterSolicitation = uint8_t{0x99U};        ///< Multicast Router Solicitation (153).
            inline constexpr uint8_t multicastRouterTermination = uint8_t{0x9AU};         ///< Multicast Router Termination (154).

            inline constexpr uint8_t rplControlMessage = uint8_t{0x9BU};                  ///< RPL Control Message (155).

            inline constexpr uint8_t extendedEchoRequest = uint8_t{0xA0U};                ///< Extended Echo Request (160).
            inline constexpr uint8_t extendedEchoReply = uint8_t{0xA1U};                  ///< Extended Echo Reply (161).
        }

        /**
         * @namespace Option
         * @brief Contains ICMPv6-related Option Constants
         */
        namespace Option
        {
            inline constexpr uint8_t source = uint8_t{0x01U};      ///< Source Option for NDP.
            inline constexpr uint8_t target = uint8_t{0x02U};      ///< Tartet Option for NDP.
            inline constexpr uint8_t prefix = uint8_t{0x03U};      ///< Prefix Information Option for NDP.
            inline constexpr uint8_t redirect = uint8_t{0x04U};    ///< Redirect Option for NDP.
            inline constexpr uint8_t mtu = uint8_t{0x05U};         ///< MTU Option for NDP.
            inline constexpr uint8_t nbma = uint8_t{0x06U};        ///< NBMA Option for NDP.
            inline constexpr uint8_t cga = uint8_t{0x0bU};         ///< CGA Option for NDP.
            inline constexpr uint8_t rsa = uint8_t{0x0cU};         ///< RSA Option for NDP.
            inline constexpr uint8_t timestamp = uint8_t{0x0dU};   ///< Timestamp Option for NDP.
            inline constexpr uint8_t nonce = uint8_t{0x0eU};       ///< Nonce Option for NDP.
            inline constexpr uint8_t trustAnchor = uint8_t{0x0fU}; ///< Trust Anchor Option for NDP.
            inline constexpr uint8_t certificate = uint8_t{0x10U}; ///< Certification Option for NDP.
            inline constexpr uint8_t routeInfo = uint8_t{0x18U};   ///< Route Information Option for NDP.
            inline constexpr uint8_t dnsServer = uint8_t{0x19U};   ///< DNS Server Option for NDP.
            inline constexpr uint8_t dnsSearch = uint8_t{0x1FU};   ///< DNS Search Option for NDP.
        }

    }

    /**
     * @namespace IP
     * @brief Contains IP protocol number constants.
     */
    namespace IP
    {
        // Existing Protocols
        inline constexpr uint8_t esp = uint8_t{0x32U};      ///< IP protocol number for ESP (50).
        inline constexpr uint8_t ah = uint8_t{0x33U};       ///< IP protocol number for AH (51).
        inline constexpr uint8_t gre = uint8_t{0x2fU};      ///< IP protocol number for GRE (47).
        inline constexpr uint8_t igmp = uint8_t{0x02U};     ///< IP protocol number for IGMP (2).
        inline constexpr uint8_t none = uint8_t{0x3bU};     ///< IP protocol number for None (59).
        inline constexpr uint8_t tcp = uint8_t{0x06U};      ///< IP protocol number for TCP (6).
        inline constexpr uint8_t udp = uint8_t{0x11U};      ///< IP protocol number for UDP (17).
        inline constexpr uint8_t icmpv4 = uint8_t{0x01U};   ///< IP protocol number for ICMPv4 (1).
        inline constexpr uint8_t icmpv6 = uint8_t{0x3aU};   ///< IP protocol number for ICMPv6 (58).
        inline constexpr uint8_t sctp = uint8_t{0x84U};     ///< IP protocol number for SCTP (132).
        inline constexpr uint8_t eigrp = uint8_t{0x58U};    ///< IP protocol number for EIGRP (88).
        inline constexpr uint8_t ospf = uint8_t{0x59U};     ///< IP protocol number for OSPF (89).
        inline constexpr uint8_t pim = uint8_t{0x67U};      ///< IP protocol number for PIM (103).
        inline constexpr uint8_t rsvp = uint8_t{0x2eU};     ///< IP protocol number for RSVP (46). Corrected from \x2f to \x2e.

        // Additional Protocols
        inline constexpr uint8_t hopopt = uint8_t{0x00U};       ///< IP protocol number for HOPOPT (0).
        inline constexpr uint8_t ggp = uint8_t{0x03U};          ///< IP protocol number for GGP (3).
        inline constexpr uint8_t ipv4 = uint8_t{0x04U};         ///< IP protocol number for IPv4 (4).
        inline constexpr uint8_t st = uint8_t{0x05U};           ///< IP protocol number for ST (5).
        inline constexpr uint8_t cbt = uint8_t{0x07U};          ///< IP protocol number for CBT (7).
        inline constexpr uint8_t egp = uint8_t{0x08U};          ///< IP protocol number for EGP (8).
        inline constexpr uint8_t igp_v2 = uint8_t{0x09U};       ///< IP protocol number for IGP (9).
        inline constexpr uint8_t bbn_rcc_mon = uint8_t{0x0aU};  ///< IP protocol number for BBN_RCC_MON (10).
        inline constexpr uint8_t nvp_ii = uint8_t{0x0bU};        ///< IP protocol number for NVP-II (11).
        inline constexpr uint8_t pup = uint8_t{0x0cU};           ///< IP protocol number for PUP (12).
        inline constexpr uint8_t argus = uint8_t{0x0dU};         ///< IP protocol number for ARGUS (13).
        inline constexpr uint8_t emcon = uint8_t{0x0eU};         ///< IP protocol number for EMCON (14).
        inline constexpr uint8_t xnet = uint8_t{0x0fU};          ///< IP protocol number for XNET (15).
        inline constexpr uint8_t chaos = uint8_t{0x10U};         ///< IP protocol number for CHAOS (16).
        inline constexpr uint8_t mux = uint8_t{0x12U};           ///< IP protocol number for MUX (18).
        inline constexpr uint8_t dcn_meas = uint8_t{0x13U};      ///< IP protocol number for DCN_MEAS (19).
        inline constexpr uint8_t hmp = uint8_t{0x14U};           ///< IP protocol number for HMP (20).
        inline constexpr uint8_t prm = uint8_t{0x15U};           ///< IP protocol number for PRM (21).
        inline constexpr uint8_t xnsIdp = uint8_t{0x16U};       ///< IP protocol number for XNS_IDP (22).
        inline constexpr uint8_t trunk1 = uint8_t{0x17U};       ///< IP protocol number for TRUNK-1 (23).
        inline constexpr uint8_t trunk2 = uint8_t{0x18U};       ///< IP protocol number for TRUNK-2 (24).
        inline constexpr uint8_t leaf1 = uint8_t{0x19U};        ///< IP protocol number for LEAF-1 (25).
        inline constexpr uint8_t leaf2 = uint8_t{0x1aU};        ///< IP protocol number for LEAF-2 (26).
        inline constexpr uint8_t rdp = uint8_t{0x1bU};           ///< IP protocol number for RDP (27).
        inline constexpr uint8_t irtp = uint8_t{0x1cU};          ///< IP protocol number for IRTP (28).
        inline constexpr uint8_t isoTp4 = uint8_t{0x1dU};       ///< IP protocol number for ISO_TP4 (29).
        inline constexpr uint8_t netblt = uint8_t{0x1eU};        ///< IP protocol number for NETBLT (30).
        inline constexpr uint8_t mfeNsp = uint8_t{0x1fU};       ///< IP protocol number for MFE_NSP (31).
        inline constexpr uint8_t meritInp = uint8_t{0x20U};     ///< IP protocol number for MERIT_INP (32).
        inline constexpr uint8_t dccp = uint8_t{0x21U};          ///< IP protocol number for DCCP (33).
        inline constexpr uint8_t ipcomp = uint8_t{0x22U};        ///< IP protocol number for IPCOMP (34).
        inline constexpr uint8_t eigrpv6 = uint8_t{0x58U};      ///< IP protocol number for EIGRP (88).
        inline constexpr uint8_t ospfv3 = uint8_t{0x59U};       ///< IP protocol number for OSPF (89).
        inline constexpr uint8_t pimSm = uint8_t{0x67U};        ///< IP protocol number for PIM-SM (103).
        inline constexpr uint8_t l2tp = uint8_t{0x4eU};          ///< IP protocol number for L2TP (78).
        inline constexpr uint8_t mplsInIp = uint8_t{0x2bU};    ///< IP protocol number for MPLS-in-IP (43).
        inline constexpr uint8_t vxlan = uint8_t{0xb7U};         ///< IP protocol number for VXLAN (183).
        inline constexpr uint8_t dvrp = uint8_t{0x5eU};          ///< IP protocol number for DVRP (94).
        inline constexpr uint8_t lmtp = uint8_t{0x46U};          ///< IP protocol number for LMTP (70).
        inline constexpr uint8_t encap = uint8_t{0x8eU};         ///< IP protocol number for ENCAPSULATION (142).
        inline constexpr uint8_t ipv6 = uint8_t{0x29U};          ///< IP protocol number for IPv6 (41).
        inline constexpr uint8_t pimDm = uint8_t{0x64U};        ///< IP protocol number for PIM-DM (100).
        inline constexpr uint8_t eigrpv5 = uint8_t{0x8aU};      ///< IP protocol number for EIGRP for IPv6 (138).
        inline constexpr uint8_t rsvp_te = uint8_t{0x73U};       ///< IP protocol number for RSVP-TE (115).
        inline constexpr uint8_t mpls = uint8_t{0x2bU};          ///< IP protocol number for MPLS (43). Note: MPLS has multiple entries.
        inline constexpr uint8_t pppoeDiscovery = uint8_t{0x11U}; ///< IP protocol number for PPPoE Discovery (17).
        inline constexpr uint8_t pppoeSession = uint8_t{0x11U};   ///< IP protocol number for PPPoE Session (17).
        inline constexpr uint8_t pppEcho = uint8_t{0x01U};        ///< IP protocol number for PPP Echo (1).
        inline constexpr uint8_t pppIpcp = uint8_t{0x21U};        ///< IP protocol number for PPP IPCP (33).
        inline constexpr uint8_t pppIpv6cp = uint8_t{0x57U};      ///< IP protocol number for PPP IPv6CP (87).
        inline constexpr uint8_t eap = uint8_t{0x88U};             ///< IP protocol number for EAP (136).
        inline constexpr uint8_t lispControl = uint8_t{0x8fU};    ///< IP protocol number for LISP Control (143).
        inline constexpr uint8_t mobileregistrationProtocol = uint8_t{0x8dU}; ///< IP protocol number for Mobile Registration Protocol (141).
    }

    /**
     * @namespace Udp
     * @brief Contains UDP port constants related to DHCP.
     */
    namespace Udp
    {
        inline constexpr uint16_t dhcpClient = uint16_t{0x0044U};     ///< UDP source port for DHCP.
        inline constexpr uint16_t dhcpServer = uint16_t{0x0043U};     ///< UDP destination port for DHCP.
        inline constexpr uint16_t dhcpv6Client = uint16_t{0x0222U};   ///< UDP source port for DHCPv6.
        inline constexpr uint16_t dhcpv6Server = uint16_t{0x0223U};   ///< UDP destination port for DHCPv6.
    }

    /**
     * @namespace Tcp
     * @brief Contains TCP-related constants.
     */
    namespace Tcp
    {

    }

    /**
     * @namespace Mac
     * @brief Contains MAC address-related constants.
     */
    namespace Mac
    {
        inline constexpr uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; ///< Broadcast MAC address (FF:FF:FF:FF:FF:FF).
        inline constexpr uint8_t source[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};    ///< Placeholder source MAC address (00:00:00:00:00:00).
    }

    /**
     * @namespace IPv4
     * @brief Contains IPv4 address-related constants.
     */
    namespace IPv4
    {
        inline constexpr uint8_t broadcast[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; ///< Broadcast IPv4 address (255.255.255.255).
        inline constexpr uint8_t source[4] = { 0x00, 0x00, 0x00, 0x00 };    ///< Placeholder source IPv4 address (0.0.0.0).
    }

    /**
     * @namespace IPv6
     * @brief Contains IPv6 address-related constants.
     */
    namespace IPv6
    {
        inline constexpr uint8_t source[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        inline constexpr uint8_t multicact[16] = {0xFF, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    }

    /**
     * @namespace Dhcp
     * @brief Contains DHCP-related constants.
     */
    namespace Dhcp
    {
        /**
         * @namespace Type
         * @brief Contains DHCP message type constants.
         */
        namespace Type
        {
            inline constexpr uint8_t discover = uint8_t{0x01U};         ///< DHCP Discover message type.
            inline constexpr uint8_t offer = uint8_t{0x02U};            ///< DHCP Offer message type.
            inline constexpr uint8_t request = uint8_t{0x03U};          ///< DHCP Request message type.
            inline constexpr uint8_t decline = uint8_t{0x04U};          ///< DHCP Decline message type.
            inline constexpr uint8_t ack = uint8_t{0x05U};              ///< DHCP Acknowledgment message type.
            inline constexpr uint8_t nak = uint8_t{0x06U};              ///< DHCP Negative Acknowledgment message type.
            inline constexpr uint8_t release = uint8_t{0x07U};          ///< DHCP Release message type
            inline constexpr uint8_t inform = uint8_t{0x08U};           ///< DHCP Inform message type
            inline constexpr uint8_t forceRenew = uint8_t{0x09U};       ///< DHCP Force Renew message type.
            inline constexpr uint8_t leaseQuery = uint8_t{0x0AU};       ///< DHCP Lease Query message type.
            inline constexpr uint8_t leaseUnassigned = uint8_t{0x0BU};  ///< DHCP Lease Unassigned message type.
            inline constexpr uint8_t leaseUnknown = uint8_t{0x0CU};     ///< DHCP Lease Unknown message type.
            inline constexpr uint8_t leaseActive = uint8_t{0x0DU};      ///< DHCP Lease Active Message type.
        }

        /**
         * @namespace Option
         * @brief Contains DHCP option codes.
         */
        namespace Option
        {
            inline constexpr uint8_t mask = uint8_t{0x01U};                   ///< DHCP Option for Subnet Mask.
            inline constexpr uint8_t broadcast = uint8_t{0x1cU};              ///< DHCP Option for Broadcast Address.
            inline constexpr uint8_t router = uint8_t{0x03U};                 ///< DHCP Option for Router.
            inline constexpr uint8_t domainName = uint8_t{0x0fU};             ///< DHCP Option for Domain Name.
            inline constexpr uint8_t domainServer = uint8_t{0x06U};           ///< DHCP Option for Domain Name Server.
            inline constexpr uint8_t domainSearch = uint8_t{0x77U};           ///< DHCP Option for Domain Search.
            inline constexpr uint8_t netbiosNameServer = uint8_t{0x2cU};      ///< DHCP Option for NetBIOS Name Server.
            inline constexpr uint8_t mtu = uint8_t{0x1aU};                    ///< DHCP Option for MTU.
            inline constexpr uint8_t classlessStateRoute = uint8_t{0x79U};    ///< DHCP Option for Classless Static Route.
            inline constexpr uint8_t ntp = uint8_t{0x2aU};                    ///< DHCP Option for NTP Servers.
            inline constexpr uint8_t overload = uint8_t{0x34U};               ///< DHCP Option for Overload.
            inline constexpr uint8_t type = uint8_t{0x35U};                   ///< DHCP Option for Message Type.
            inline constexpr uint8_t hostname = uint8_t{0x0cU};               ///< DHCP Option for Hostname.
            inline constexpr uint8_t clientID = uint8_t{0x3dU};               ///< DHCP Option for Client Identifier.
            inline constexpr uint8_t serverIdentifier = uint8_t{0x36U};       ///< DHCP Option for Server Identifier.
            inline constexpr uint8_t leaseTime = uint8_t{0x33U};              ///< DHCP Option for Lease Time.
            inline constexpr uint8_t renewalTime = uint8_t{0x3aU};            ///< DHCP Option for Renewal Time.
            inline constexpr uint8_t rebindingTime = uint8_t{0x3bU};          ///< DHCP Option for Rebinding Time.
            inline constexpr uint8_t requestIP = uint8_t{0x32U};              ///< DHCP Option for Requested IP Address.
            inline constexpr uint8_t requestList = uint8_t{0x37U};            ///< DHCP Option for Parameter Request List.
            inline constexpr uint8_t maxSize = uint8_t{0x39U};                ///< DHCP Option for Maximum DHCP Message Size.
            inline constexpr uint8_t tftpServerName = uint8_t{0x42U};         ///< DHCP Option for TFTP server.
            inline constexpr uint8_t bootfile = uint8_t{0x43U};               ///< DHCP Option for Bootfile.
            inline constexpr uint8_t staticRoute = uint8_t{0x21U};            ///< DHCP Option for obtaining static routes.
            inline constexpr uint8_t vendorSpecific = uint8_t{0x2bU};         ///< DHCP Option for vendor specific information.
            inline constexpr uint8_t vendorClassID = uint8_t{0x3CU};          ///< DHCP Option for vendor class identifier.
            inline constexpr uint8_t authentication = uint8_t{0x5aU};         ///< DHCP Option for Authentication.
            inline constexpr uint8_t rapidCommit = uint8_t{0x50U};            ///< DHCP Option for rapid commit.
            inline constexpr uint8_t relayAgentInfo = uint8_t{0x52U};         ///< DHCP Option for relay agent info.
            inline constexpr uint8_t timestamp = uint8_t{0x5BU};              ///< DHCP Option for timestamp info.
            inline constexpr uint8_t tftpServers = uint8_t{0x96U};            ///< DHCP Option for tftp servers.
        }

        /**
         * @namespace Timers
         * @brief Contains DHCP timer constraints.
         */
        namespace Timers
        {
            inline constexpr uint32_t minLeaseTime = 3600;
            inline constexpr uint32_t maxLeaseTime = 86400;
        }

        inline constexpr uint8_t clientHardwareAddressPadding[10] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Padding for Client Hardware Address.
        inline constexpr uint8_t serverHostName[64] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Server Name.
        inline constexpr uint8_t bootfile[128] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Bootfile Name.
        inline constexpr uint8_t endPadding[25] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Padding after DHCP options.
        inline constexpr uint8_t end = uint8_t{0xffU}; ///< DHCP Option End Marker.
        inline constexpr uint32_t magicCookie(0x63825363U);       ///< DHCP Magic Cookie.    
    }

    /**
     * @namespace Dhcpv6
     * @brief Contains DHCPv6-related constants.
     */
    namespace Dhcpv6
    {
        /**
         * @namespece Type
         * @brief Contains DHCPv6 type codes.
         */
        namespace Type
        {
            inline constexpr uint8_t solicit = uint8_t{0x01U};             ///< DHCPv6 Solicit message type.
            inline constexpr uint8_t advertise = uint8_t{0x02U};           ///< DHCPv6 Advertise message type.
            inline constexpr uint8_t request = uint8_t{0x03U};             ///< DHCPv6 Request message type.
            inline constexpr uint8_t confirm = uint8_t{0x04U};             ///< DHCPv6 Confirm message type.
            inline constexpr uint8_t renew = uint8_t{0x05U};               ///< DHCPv6 Renew message type.
            inline constexpr uint8_t rebind = uint8_t{0x06U};              ///< DHCPv6 Rebind message type.
            inline constexpr uint8_t reply = uint8_t{0x07U};               ///< DHCPv6 Reply message type.
            inline constexpr uint8_t release = uint8_t{0x08U};             ///< DHCPv6 Release message type.
            inline constexpr uint8_t decline = uint8_t{0x09U};             ///< DHCPv6 Decline message type.
            inline constexpr uint8_t reconfigure = uint8_t{0x0AU};         ///< DHCPv6 Reconfigure message type.
            inline constexpr uint8_t informationRequest = uint8_t{0x0BU};  ///< DHCPv6 Information request message type.
            inline constexpr uint8_t relayForward = uint8_t{0x0CU};        ///< DHCPv6 Relay forward message type.
            inline constexpr uint8_t relayReply = uint8_t{0x0DU};          ///< DHCPv6 Relay reply message type.
        }

        /**
         * @namespace Options
         * @brief Contains DHCPv6 option codes.
         */
        namespace Options
        {
            inline constexpr uint16_t clientID = uint16_t{0x0001U};            ///< OPTION_CLIENTID (1)
            inline constexpr uint16_t serverID = uint16_t{0x0002U};            ///< OPTION_SERVERID (2)
            inline constexpr uint16_t IA_NA = uint16_t{0x0003U};               ///< OPTION_IA_NA (3)
            inline constexpr uint16_t IA_TA = uint16_t{0x0004U};               ///< OPTION_IA_TA (4)
            inline constexpr uint16_t IAAddr = uint16_t{0x0005U};              ///< OPTION_IAADDR (5)
            inline constexpr uint16_t optionRequest = uint16_t{0x0006U};       ///< OPTION_ORO (6)
            inline constexpr uint16_t preference = uint16_t{0x0007U};          ///< OPTION_PREFERENCE (7)
            inline constexpr uint16_t elapsedTime = uint16_t{0x0008U};         ///< OPTION_ELAPSED_TIME (8)
            inline constexpr uint16_t relayMsg = uint16_t{0x0009U};            ///< OPTION_RELAY_MSG (9)
            inline constexpr uint16_t auth = uint16_t{0x000BU};                ///< OPTION_AUTH (11)
            inline constexpr uint16_t unicast = uint16_t{0x000CU};             ///< OPTION_UNICAST (12)
            inline constexpr uint16_t statusCode = uint16_t{0x000DU};          ///< OPTION_STATUS_CODE (13)
            inline constexpr uint16_t rapidCommit = uint16_t{0x000EU};         ///< OPTION_RAPID_COMMIT (14)
            inline constexpr uint16_t vendorOpts = uint8_t{0x0010U};           ///< OPTION_VENDOR_OPTS (16)
            inline constexpr uint16_t vendorClassID = uint8_t{0x0011U};        ///< OPTION_VENDOR_CLASS (17)
            inline constexpr uint16_t reconfigureMessage = uint16_t{0x0013U};  ///< OPTION_RECONF_MSG (19)
            inline constexpr uint16_t reconfAccept = uint16_t{0x0014U};        ///< OPTION_RECONF_ACCEPT (20)
            inline constexpr uint16_t dnsServer = uint16_t{0x0017U};           ///< OPTION_DNS_SERVERS (23)
            inline constexpr uint16_t domainSearch = uint16_t{0x0018U};        ///< OPTION_DOMAIN_LIST (24)
            inline constexpr uint16_t IA_PD = uint16_t{0x0019U};               ///< OPTION_IA_PD (25)
            inline constexpr uint16_t fqdn = uint16_t{0x0027U};                ///< OPTION_CLIENT_FQDN (39)
            inline constexpr uint16_t IA_Prefix = uint16_t{0x001AU};           ///< OPTION_IAPREFIX (26)
            inline constexpr uint16_t infoRefreshTime = uint16_t{0x0020U};     ///< OPTION_INFO_REFRESH (32)
            inline constexpr uint16_t ntpServer = uint16_t{0x0038U};           ///< OPTION_NTP_SERVER (56)
            inline constexpr uint16_t solMaxRt = uint16_t{0x0052U};            ///< OPTION_SOL_MAX_RT (82)
            inline constexpr uint16_t infMaxRt = uint16_t{0x0053U};            ///< OPTION_INF_MAX_RT (83)...
            inline constexpr uint16_t remoteID = uint16_t{0x0025U};            ///< OPTION_REMOTE_ID (37)
            inline constexpr uint16_t interfaceID = uint16_t{0x0012U};         ///< OPTION_INTERFACE_ID (18)...
            inline constexpr uint16_t leaseQuery = uint16_t{0x002cU};          ///< OPTION_LQ_QUERY (44)
            inline constexpr uint16_t leaseClientData = uint16_t{0x002dU};     ///< OPTION_QLIENT_DATA (45)
            inline constexpr uint16_t leaseQueryData = uint16_t{0x002eU};      ///< OPTION_LQ_RELAY_DATA (46)...
            inline constexpr uint16_t queryByAddr = uint16_t{0x0001U};         ///< QUERY_BY_ADDRESS (1)
            inline constexpr uint16_t queryByClientID = uint16_t{0x0002U};     ///< QUERY_BY_CLIENTID (2)
        }

        /**
         * @namespace Status
         * @brief Contains DHCPv6 status codes.
         */
        namespace Status 
        {
            inline constexpr uint16_t success = uint16_t{0x0000U};             ///< Status code for success.
            inline constexpr uint16_t unspecFail = uint16_t{0x0001U};          ///< Status code for unspecified failure.
            inline constexpr uint16_t noAddrsAvail = uint16_t{0x0002U};        ///< Status code for no address available.
            inline constexpr uint16_t noBinding = uint16_t{0x0003U};           ///< Status code for no binding available.
            inline constexpr uint16_t notOnLink = uint16_t{0x0004U};           ///< Status code for not on link.
            inline constexpr uint16_t useMulticast = uint16_t{0x0005U};        ///< Status code for using multicast.
            inline constexpr uint16_t noPrefixAvail = uint16_t{0x0006U};       ///< Status code for no prefix available.
        }

        /**
         * @namespace Timers
         * @brief Contains DHCPv6 timer constraints.
         */
        namespace Timers
        {        
            inline constexpr uint32_t solMaxDelay = 1;      // Max delay of first Solicit
            inline constexpr uint32_t solTimeout = 1;       // Initial Solicit timeout
            inline constexpr uint32_t solMaxRt = 3600;      // Max Solicit timeout value
            inline constexpr uint32_t reqTimeout = 1;       // Initial Request timeout
            inline constexpr uint32_t reqMaxTimeout = 30;   // Max Request timeout
            inline constexpr uint32_t reqMaxRc = 10;        // Max Request retry attempts
            inline constexpr uint32_t cfnMaxDelay = 1;      // Max delay of first Confirm
            inline constexpr uint32_t cnfTimeout = 1;       // Initial Confirm timeout
            inline constexpr uint32_t cnfMaxRt = 4;         // Max Confirm timeout
            inline constexpr uint32_t cnfMaxRd = 10;        // Max Confirm duration
            inline constexpr uint32_t renTimeout = 10;      // Initial Renew timeout
            inline constexpr uint32_t renMaxRt = 600;       // Max Renew timeout
            inline constexpr uint32_t rebTimeout = 10;      // Initial Rebind timeout
            inline constexpr uint32_t rebMaxRt = 600;       // Max Rebind timeout
        }
    }

    /**
     * @namespace Eigrp
     * @brief Contains EIGRP-related constants.
     */
    namespace Eigrp
    {
        /**
         * @namespace Type
         * @brief Contains EIGRP message type constants.
         */
        namespace Type
        {
            inline constexpr uint8_t update = uint8_t{0x01U};   ///< EIGRP Update message type.
            inline constexpr uint8_t request = uint8_t{0x02U};  ///< EIGRP Request message type.
            inline constexpr uint8_t query = uint8_t{0x03U};    ///< EIGRP Query message type.
            inline constexpr uint8_t reply = uint8_t{0x04U};    ///< EIGRP Reply message type.
            inline constexpr uint8_t hello = uint8_t{0x05U};    ///< EIGRP Hello message type.
            inline constexpr uint8_t siaQuery = uint8_t{0xa0U}; ///< Eigrp SIAQuery message type.
            inline constexpr uint8_t siaReply = uint8_t{0xa1U}; ///< Eigrp SIAReply message type
        }

        /**
         * @namespace Option
         * @brief Contains EIGRP option codes.
         */
        namespace Option
        {
            inline constexpr uint16_t parameter = uint16_t{0x0001U};             ///< EIGRP Option for Parameter.
            inline constexpr uint16_t version = uint16_t{0x0004U};               ///< EIGRP Option for Version.
            inline constexpr uint16_t sequence = uint16_t{0x0003U};              ///< EIGRP Option for Sequence Number.
            inline constexpr uint16_t multicastSequence = uint16_t{0x0005U};     ///< EIGRP Option for Multicast Sequence.
            inline constexpr uint16_t legacyInternalRoute = uint16_t{0x0102U};   ///< EIGRP Option for Internal Route.
            inline constexpr uint16_t legacyExternalRoute = uint16_t{0x0103U};   ///< EIGRP Option for External Route.
            inline constexpr uint16_t legacyInternalRouteV6 = uint16_t{0x0402U}; ///< EIGRP Option for Legacy Internal Route IPv6.
            inline constexpr uint16_t legacyExternalRouteV6 = uint16_t{0x0403U}; ///< EIGRP Option for Legacy External Route IPv6.
            inline constexpr uint16_t internalRoute = uint16_t{0x0602U};         ///< EIGRP Option for Legacy Internal Route IPv6.
            inline constexpr uint16_t externalRoute = uint16_t{0x0603U};         ///< EIGRP Option for Legacy External Route IPv6.
            inline constexpr uint16_t stub = uint16_t{0x0006U};                  ///< EIGRP Option for Stub.
            inline constexpr uint16_t authentication = uint16_t{0x0002U};        ///< EIGRP Option for Authentication.
        }

        /**
         * @namespace Version
         * @brief Contains EIGRP version constants.
         */
        namespace Version
        {
            inline constexpr uint16_t release = uint16_t{0x0c04U};  ///< EIGRP Version Release.
            inline constexpr uint16_t tls = uint16_t{0x0102U};      ///< EIGRP Version TLS.
        }

        /**
         * @namespace ExternalProtocol
         * @brief Contains EIGRP external protocol constants.
         */
        namespace ExternalProtocol 
        {
            inline constexpr uint8_t igrp = uint8_t{0x01U};          ///< EIGRP External Protocol IGRP.
            inline constexpr uint8_t eigrp = uint8_t{0x02U};         ///< EIGRP External Protocol EIGRP.
            inline constexpr uint8_t staticRoute = uint8_t{0x03U};   ///< EIGRP External Protocol Static Route.
            inline constexpr uint8_t rip = uint8_t{0x04U};           ///< EIGRP External Protocol RIP.
            inline constexpr uint8_t hello = uint8_t{0x05U};         ///< EIGRP External Protocol Hello.
            inline constexpr uint8_t ospf = uint8_t{0x06U};          ///< EIGRP External Protocol OSPF.
            inline constexpr uint8_t isis = uint8_t{0x07U};          ///< EIGRP External Protocol ISIS.
            inline constexpr uint8_t bgp = uint8_t{0x09U};           ///< EIGRP External Protocol BGP.
            inline constexpr uint8_t connected = uint8_t{0x0bU};     ///< EIGRP External Protocol Connected.
        }

        /**
         * @namespace DestinationAssignmentEncoding
         * @brief Contains EIGRP Destination Assignment Encoding constants.
         */
        namespace DestinationAssignmentEncoding 
        {
            inline constexpr uint8_t ipv4 = uint8_t{0x01U};              ///< EIGRP Destination Assignment Encoding IPv4.
            inline constexpr uint8_t ipv6 = uint8_t{0x02U};              ///< EIGRP Destination Assignment Encoding IPv6.
            inline constexpr uint16_t commonService = uint16_t{0x4000U}; ///< EIGRP Destination Assignment Encoding Common Service.
            inline constexpr uint16_t ipv4Family = uint16_t{0x4001U};    ///< EIGRP Destination Assignment Encoding IPv4 Family.
            inline constexpr uint16_t ipv6Famil = uint16_t{0x4002U};     ///< EIGRP Destination Assignment Encoding IPv6 Family.
        }

        /**
         * @namespace CommunityAttribute
         * @brief Contains EIGRP Community Attribute constants.
         */
        namespace CommunityAttribute 
        {
            inline constexpr uint8_t EXTCOMM_EIGRP = uint8_t{0x00U};       ///< EIGRP Community Attribute for EIGRP.
            inline constexpr uint8_t EXTCOMM_DAD = uint8_t{0x01U};         ///< EIGRP Community Attribute for DAD.
            inline constexpr uint8_t EXTCOMM_VRHB = uint8_t{0x02U};        ///< EIGRP Community Attribute for VRHB.
            inline constexpr uint8_t EXTCOMM_SRLM = uint8_t{0x03U};        ///< EIGRP Community Attribute for SRLM.
            inline constexpr uint8_t EXTCOMM_SAR = uint8_t{0x04U};         ///< EIGRP Community Attribute for SAR.
            inline constexpr uint8_t EXTCOMM_RPM = uint8_t{0x05U};         ///< EIGRP Community Attribute for RPM.
            inline constexpr uint8_t EXTCOMM_VRR = uint8_t{0x06U};         ///< EIGRP Community Attribute for VRR.
        }

        /**
         * @namespace Dampening
         * @brief Contains Eigrp Dampening constants
         */
        namespace Dampening
        {
            inline constexpr uint32_t flapPenalty = 1000; ///< Eigrp dampening flap penalty.
            inline constexpr uint32_t supressThreshold = 2000; ///< Supression threshold from penalty.
            inline constexpr uint32_t reuseThreshold = 750; ///< Penalty needed to be unsupressed.
            inline constexpr uint32_t decayInterval = 5; ///< Penalty decays every 5 seconds.
            inline constexpr uint32_t halflifeTime = 15; ///< Penalty is halfed every 15 seconds.
            inline constexpr uint32_t maxSuppressTime = 10; ///< Maximum time a route can be supressed.
        }
    }

    /**
     * @namespace Multicast
     * @brief Contains multicast-related constants.
     */
    namespace Multicast
    {
        /**
         * @namespace Eigrp
         * @brief Contains EIGRP multicast address and MAC constants.
         */
        namespace Eigrp
        {
            inline constexpr uint8_t address[4] = { 0xE0, 0x00, 0x00, 0x0A }; ///< EIGRP Multicast IPv4 Address.
            inline constexpr uint8_t addressv6[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A }; ///< EIGRP Multicast IPv6 Address.
            inline constexpr uint8_t mac[6] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0x0A }; ///< EIGRP Multicast MAC Address.
            inline constexpr uint8_t macv6[6] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x0A }; ///< EIGRP Multicast MAC Address for IPv6.
        }

        /**
         * @namespace ICMPv6
         * @brief Contains multicast address constants for ICMPv6
         */
        namespace ICMPv6
        {
            inline constexpr uint8_t solicitationAddress[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00 };
            inline constexpr uint8_t allRouters[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };
        }

        /**
         * @namespace DHCP
         * @brief Contains DHCP multicast address and constants.
         */
        namespace Dhcp
        {
            inline constexpr uint8_t clientToServerv6[16] = { 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02 };
            inline constexpr uint8_t relayToServerv6[16] = { 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03 };
            inline constexpr uint8_t serverToAllv6[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
        }
    }
}

// ------------------------- Structure Definitions -------------------------

enum class HeaderType : uint8_t
{
    NONE,
    ETHERNET,
    ARP, MPLS,
    IPV4, IPV6, AH, ESP, ICMP, ICMPV6,
    TCP, UDP, EIGRP,
    DHCP, DHCPV6, DHCPV6_RELAY,
    ENCAPSULATE
};

enum class HeaderLayer
{
    LAYER2,
    LAYER2_5,
    LAYER3,
    LAYER4,
    LAYER5
};

inline size_t getHeaderSize(HeaderType type)
{
    switch (type)
    {
        case HeaderType::ETHERNET: return sizeof(EthernetHeader);
        case HeaderType::ARP: return sizeof(ArpHeader);
        case HeaderType::MPLS: return sizeof(MplsHeader);
        case HeaderType::IPV4: return sizeof(IPv4Header);
        case HeaderType::IPV6: return sizeof(IPv6Header);
        case HeaderType::AH: return sizeof(AhHeader);
        case HeaderType::ESP: return sizeof(EspHeader);
        case HeaderType::ICMP: return sizeof(IcmpHeader);
        case HeaderType::ICMPV6: return sizeof(Icmpv6Header);
        case HeaderType::TCP: return sizeof(TcpHeader);
        case HeaderType::UDP: return sizeof(UdpHeader);
        case HeaderType::EIGRP: return sizeof(EigrpHeader);
        case HeaderType::DHCP: return sizeof(DhcpHeader);
        case HeaderType::DHCPV6: return sizeof(Dhcpv6Header);
        case HeaderType::DHCPV6_RELAY: return sizeof(Dhcpv6RelayHeader);
        default: return 0;
    }
}

struct HeaderEntry
{
    HeaderType type;
    size_t offset;
    size_t size;
};

struct PacketInfo
{
    HeaderEntry headers[MaxHeaders];
    uint8_t count = 0;
    size_t offset = 0;
};

#endif // PACKET_STRUCTUrE_H
