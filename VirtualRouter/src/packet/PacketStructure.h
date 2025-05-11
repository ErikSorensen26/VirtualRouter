// PacketStructure.h

#ifndef PACKET_STRUCTURE_H
#define PACKET_STRUCTURE_H

#include <any>
#include <ByteString.hpp>
#include <Checksums.h>
#include <variant>
#include <Profiler.hpp>

#include "EthernetHeader.hpp"
#include "ArpHeader.hpp"
#include "MplsHeader.hpp"
#include "IPv4Header.hpp"
#include "IPv6Header.hpp"
#include "TcpHeader.hpp"
#include "UdpHeader.hpp"
#include "IcmpHeader.hpp"
#include "Icmpv6Header.hpp"
#include "IgmpHeader.hpp"
#include "TlsHeader.hpp"
#include "GreHeader.hpp"
#include "AhHeader.hpp"
#include "EspHeader.hpp"
#include "VlanHeader.hpp"
#include "DhcpHeader.hpp"
#include "Dhcpv6Header.hpp"
#include "Dhcpv6RelayHeader.hpp"
#include "EigrpHeader.hpp"
#include "OspfHeader.hpp"
#include "SyslogHeader.hpp"


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
        inline const ByteString arp("\x08\x06", 2);    ///< EtherType for ARP
        inline const ByteString ipv4("\x08\x00", 2);   ///< EtherType for IPv4
        inline const ByteString ipv6("\x86\xdd", 2);   ///< EtherType for IPv6
        inline const ByteString mpls("\x88\x47", 2);   ///< EtherType for MPLS
        inline const ByteString vlan("\x81\x00", 2);   ///< EtherType fr VLAN
        inline const ByteString lldp("\x88\xcc", 2);   ///< EtherType for LLDP
    }
    
    /**
     * @namespace Arp
     * @brief Contains ARP-related constants.
     */
    namespace Arp
    {
        inline const ByteString ethernet("\x00\x01", 2);   ///< Hardware type for Ethernet
        inline const ByteString ipv4("\x08\x00", 2);       ///< Hardware type for IPv4

        /**
         * @namespace Opcode
         * @brief Contains ARP opcode constants.
         */
        namespace Opcode
        {
            inline const ByteString request("\x00\x01", 2);           ///< ARP Request.
            inline const ByteString reply("\x00\x02", 2);             ///< ARP Reply.
            inline const ByteString reverseRequest("\x00\x03", 2);    ///< Reverse ARP Request.
            inline const ByteString reverseReply("\x00\x04", 2);      ///< Reverse ARP Reply.
            inline const ByteString dynamicRequest("\x00\x05", 2);    ///< Dynamic ARP Request.
            inline const ByteString dynamicReply("\x00\x06", 2);      ///< Dynamic ARP Reply.
            inline const ByteString dynamicError("\x00\x07", 2);      ///< Dynamic ARP Error.
            inline const ByteString inverseRequest("\x00\x08", 2);    ///< Inverse ARP Request.
            inline const ByteString inverseReply("\x00\x09", 2);      ///< Inverse ARP Reply.
            inline const ByteString nak("\x00\x0a", 2);               ///< ARP NAK (Negative Acknowledgment).
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
            inline const ByteString ureachable("\x01", 1);                         ///< Unreachable error code for ICMPv6.
            inline const ByteString packetTooBig("\x02", 1);                       ///< Packet Too Big error for ICMPv6.
            inline const ByteString timeExceeded("\x03", 1);                       ///< Time Exceeded error for ICMPv6.
            inline const ByteString parameterProblem("\x04", 1);                   ///< Parameter Problem for ICMPv6.
            
            inline const ByteString echoRequest("\x80", 1);                        ///< Echo request for Ping (128).
            inline const ByteString echoReply("\x81", 1);                          ///< Echo reply for Pint (129).

            inline const ByteString mldListenQuery("\x82", 1);                     ///< MLD Listen Query (130).
            inline const ByteString mldListenReport("\x83", 1);                    ///< MLD Listen Report (131).
            inline const ByteString mldListenDone("\x84");                         ///< MLD Listen Done (132).
            inline const ByteString mldListenReportV2("\x8F", 1);                  ///< MDL Listen Report V2 (143);

            inline const ByteString ndpRouteSolicitation("\x85", 1);               ///< NDP Route Solicitation (133).
            inline const ByteString ndpRouteAdvertisement("\x86", 1);              ///< NDP Route Advertisement (134).
            inline const ByteString ndpNeighborSolicitation("\x87", 1);            ///< NDP Neighbor Solicitation (135).
            inline const ByteString ndpNeighborAdvertisement("\x88", 1);           ///< NDP Neighbor Advertisement (136).
            inline const ByteString ndpRedirectMessage("\x89", 1);                 ///< NDP Message Redirect (137).

            inline const ByteString nodeInformationQuery("\x8B", 1);               ///< Node Information Query (139).
            inline const ByteString nodeInformationResponse("\x8C", 1);            ///< Node Information Response (140).

            inline const ByteString routerRenumbering("\x8D", 1);                  ///< Router Renumbering (141).

            inline const ByteString nodeInfoQuery("\x8B", 1);                      ///< Node Information Query (139).
            inline const ByteString nodeInfoResponse("\x8C", 1);                   ///< Node Information Response (140).

            inline const ByteString homeAgentAddressDiscoveryRequest("\x90", 1);   ///< Home Agent Address Discovery Request (144).
            inline const ByteString homeAgentAddressDiscoveryReply("\x91", 1);     ///< Home Agent Address Discovery Reply (145).
            inline const ByteString mobilePrefixSolicitation("\x92", 1);           ///< Mobile Prefix Solicitation (146).
            inline const ByteString mobilePrefixAdvertisement("\x93", 1);          ///< Mobile Prefix Advertisement (147).

            inline const ByteString certificationPathSolicitation("\x94", 1);      ///< Certification Path Solicitation (148).
            inline const ByteString certificationPathAdvertisement("\x95", 1);     ///< Certification Path Advertisement (149).

            inline const ByteString icmpExperiment1("\x96", 1);                    ///< ICMP Experimentation (150).
            inline const ByteString icmpExperiment2("\x97", 1);                    ///< ICMP Experimentation (151).

            inline const ByteString multicastRouterAdvertisement("\x98", 1);       ///< Multicast Router Advertisement (152).
            inline const ByteString multicastRouterSolicitation("\x99", 1);        ///< Multicast Router Solicitation (153).
            inline const ByteString multicastRouterTermination("\x9A", 1);         ///< Multicast Router Termination (154).

            inline const ByteString rplControlMessage("\x9B", 1);                  ///< RPL Control Message (155).

            inline const ByteString extendedEchoRequest("\xA0", 1);                ///< Extended Echo Request (160).
            inline const ByteString extendedEchoReply("\xA1", 1);                  ///< Extended Echo Reply (161).
        }

        /**
         * @namespace Option
         * @brief Contains ICMPv6-related Option Constants
         */
        namespace Option
        {
            inline const ByteString source("\x01", 1);      ///< Source Option for NDP.
            inline const ByteString target("\x02", 1);      ///< Tartet Option for NDP.
            inline const ByteString prefix("\x03", 1);      ///< Prefix Information Option for NDP.
            inline const ByteString redirect("\x04", 1);    ///< Redirect Option for NDP.
            inline const ByteString mtu("\x05", 1);         ///< MTU Option for NDP.
            inline const ByteString nbma("\x06", 1);        ///< NBMA Option for NDP.
            inline const ByteString cga("\x0b", 1);         ///< CGA Option for NDP.
            inline const ByteString rsa("\x0c", 1);         ///< RSA Option for NDP.
            inline const ByteString timestamp("\x0d", 1);   ///< Timestamp Option for NDP.
            inline const ByteString nonce("\x0e", 1);       ///< Nonce Option for NDP.
            inline const ByteString trustAnchor("\x0f", 1); ///< Trust Anchor Option for NDP.
            inline const ByteString certificate("\x10", 1); ///< Certification Option for NDP.
            inline const ByteString routeInfo("\x18", 1);   ///< Route Information Option for NDP.
            inline const ByteString dnsServer("\x19", 1);   ///< DNS Server Option for NDP.
            inline const ByteString dnsSearch("\x1F", 1);   ///< DNS Search Option for NDP.
        }

    }

    /**
     * @namespace IP
     * @brief Contains IP protocol number constants.
     */
    namespace IP
    {
        // Existing Protocols
        inline const ByteString esp("\x32", 1);      ///< IP protocol number for ESP (50).
        inline const ByteString ah("\x33", 1);       ///< IP protocol number for AH (51).
        inline const ByteString gre("\x2f", 1);      ///< IP protocol number for GRE (47).
        inline const ByteString igmp("\x02", 1);     ///< IP protocol number for IGMP (2).
        inline const ByteString none("\x3b", 1);     ///< IP protocol number for None (59).
        inline const ByteString tcp("\x06", 1);      ///< IP protocol number for TCP (6).
        inline const ByteString udp("\x11", 1);      ///< IP protocol number for UDP (17).
        inline const ByteString icmpv4("\x01", 1);   ///< IP protocol number for ICMPv4 (1).
        inline const ByteString icmpv6("\x3a", 1);   ///< IP protocol number for ICMPv6 (58).
        inline const ByteString sctp("\x84", 1);     ///< IP protocol number for SCTP (132).
        inline const ByteString eigrp("\x58", 1);    ///< IP protocol number for EIGRP (88).
        inline const ByteString ospf("\x59", 1);     ///< IP protocol number for OSPF (89).
        inline const ByteString pim("\x67", 1);      ///< IP protocol number for PIM (103).
        inline const ByteString rsvp("\x2e", 1);     ///< IP protocol number for RSVP (46). Corrected from \x2f to \x2e.

        // Additional Protocols
        inline const ByteString hopopt("\x00", 1);       ///< IP protocol number for HOPOPT (0).
        inline const ByteString ggp("\x03", 1);          ///< IP protocol number for GGP (3).
        inline const ByteString ipv4("\x04", 1);         ///< IP protocol number for IPv4 (4).
        inline const ByteString st("\x05", 1);           ///< IP protocol number for ST (5).
        inline const ByteString cbt("\x07", 1);          ///< IP protocol number for CBT (7).
        inline const ByteString egp("\x08", 1);          ///< IP protocol number for EGP (8).
        inline const ByteString igp_v2("\x09", 1);       ///< IP protocol number for IGP (9).
        inline const ByteString bbn_rcc_mon("\x0a", 1);  ///< IP protocol number for BBN_RCC_MON (10).
        inline const ByteString nvp_ii("\x0b", 1);        ///< IP protocol number for NVP-II (11).
        inline const ByteString pup("\x0c", 1);           ///< IP protocol number for PUP (12).
        inline const ByteString argus("\x0d", 1);         ///< IP protocol number for ARGUS (13).
        inline const ByteString emcon("\x0e", 1);         ///< IP protocol number for EMCON (14).
        inline const ByteString xnet("\x0f", 1);          ///< IP protocol number for XNET (15).
        inline const ByteString chaos("\x10", 1);         ///< IP protocol number for CHAOS (16).
        inline const ByteString mux("\x12", 1);           ///< IP protocol number for MUX (18).
        inline const ByteString dcn_meas("\x13", 1);      ///< IP protocol number for DCN_MEAS (19).
        inline const ByteString hmp("\x14", 1);           ///< IP protocol number for HMP (20).
        inline const ByteString prm("\x15", 1);           ///< IP protocol number for PRM (21).
        inline const ByteString xnsIdp("\x16", 1);       ///< IP protocol number for XNS_IDP (22).
        inline const ByteString trunk1("\x17", 1);       ///< IP protocol number for TRUNK-1 (23).
        inline const ByteString trunk2("\x18", 1);       ///< IP protocol number for TRUNK-2 (24).
        inline const ByteString leaf1("\x19", 1);        ///< IP protocol number for LEAF-1 (25).
        inline const ByteString leaf2("\x1a", 1);        ///< IP protocol number for LEAF-2 (26).
        inline const ByteString rdp("\x1b", 1);           ///< IP protocol number for RDP (27).
        inline const ByteString irtp("\x1c", 1);          ///< IP protocol number for IRTP (28).
        inline const ByteString isoTp4("\x1d", 1);       ///< IP protocol number for ISO_TP4 (29).
        inline const ByteString netblt("\x1e", 1);        ///< IP protocol number for NETBLT (30).
        inline const ByteString mfeNsp("\x1f", 1);       ///< IP protocol number for MFE_NSP (31).
        inline const ByteString meritInp("\x20", 1);     ///< IP protocol number for MERIT_INP (32).
        inline const ByteString dccp("\x21", 1);          ///< IP protocol number for DCCP (33).
        inline const ByteString ipcomp("\x22", 1);        ///< IP protocol number for IPCOMP (34).
        inline const ByteString eigrpv6("\x58", 1);      ///< IP protocol number for EIGRP (88).
        inline const ByteString ospfv3("\x59", 1);       ///< IP protocol number for OSPF (89).
        inline const ByteString pimSm("\x67", 1);        ///< IP protocol number for PIM-SM (103).
        inline const ByteString l2tp("\x4e", 1);          ///< IP protocol number for L2TP (78).
        inline const ByteString mplsInIp("\x2b", 1);    ///< IP protocol number for MPLS-in-IP (43).
        inline const ByteString vxlan("\xb7", 1);         ///< IP protocol number for VXLAN (183).
        inline const ByteString dvrp("\x5e", 1);          ///< IP protocol number for DVRP (94).
        inline const ByteString lmtp("\x46", 1);          ///< IP protocol number for LMTP (70).
        inline const ByteString encap("\x8e", 1);         ///< IP protocol number for ENCAPSULATION (142).
        inline const ByteString ipv6("\x29", 1);          ///< IP protocol number for IPv6 (41).
        inline const ByteString pimDm("\x64", 1);        ///< IP protocol number for PIM-DM (100).
        inline const ByteString eigrpv5("\x8a", 1);      ///< IP protocol number for EIGRP for IPv6 (138).
        inline const ByteString rsvp_te("\x73", 1);       ///< IP protocol number for RSVP-TE (115).
        inline const ByteString mpls("\x2b", 1);          ///< IP protocol number for MPLS (43). Note: MPLS has multiple entries.
        inline const ByteString pppoeDiscovery("\x11", 1); ///< IP protocol number for PPPoE Discovery (17).
        inline const ByteString pppoeSession("\x11", 1);   ///< IP protocol number for PPPoE Session (17).
        inline const ByteString pppEcho("\x01", 1);        ///< IP protocol number for PPP Echo (1).
        inline const ByteString pppIpcp("\x21", 1);        ///< IP protocol number for PPP IPCP (33).
        inline const ByteString pppIpv6cp("\x57", 1);      ///< IP protocol number for PPP IPv6CP (87).
        inline const ByteString eap("\x88", 1);             ///< IP protocol number for EAP (136).
        inline const ByteString lispControl("\x8f", 1);    ///< IP protocol number for LISP Control (143).
        inline const ByteString mobileregistrationProtocol("\x8d", 1); ///< IP protocol number for Mobile Registration Protocol (141).
    }

    /**
     * @namespace Udp
     * @brief Contains UDP port constants related to DHCP.
     */
    namespace Udp
    {
        inline const ByteString dhcpClient("\x00\x44", 2);     ///< UDP source port for DHCP.
        inline const ByteString dhcpServer("\x00\x43", 2);     ///< UDP destination port for DHCP.
        inline const ByteString dhcpv6Client("\x02\x22", 2);   ///< UDP source port for DHCPv6.
        inline const ByteString dhcpv6Server("\x02\x23", 2);   ///< UDP destination port for DHCPv6
    }

    /**
     * @namespace Tcp
     * @brief Contains TCP-related constants.
     */
    namespace Tcp
    {

    }

    /**
     * @namespace Gre
     * @brief Contains GRE-related constants.
     */
    namespace Gre
    {
        inline const ByteString ppp("\x88\x0b", 2); ///< GRE protocol type for PPP.
    }

    /**
     * @namespace Ah
     * @brief Contains AH-related constants.
     */
    namespace Ah
    {
        inline const ByteString esp("\x32", 1); ///< AH protocol number for ESP.
    }

    /**
     * @namespace Mac
     * @brief Contains MAC address-related constants.
     */
    namespace Mac
    {
        inline const ByteString broadcast(6, '\xff'); ///< Broadcast MAC address (FF:FF:FF:FF:FF:FF).
        inline const ByteString source(6, '\x00');    ///< Placeholder source MAC address (00:00:00:00:00:00).
    }

    /**
     * @namespace IPv4
     * @brief Contains IPv4 address-related constants.
     */
    namespace IPv4
    {
        inline const ByteString broadcast(4, '\xff'); ///< Broadcast IPv4 address (255.255.255.255).
        inline const ByteString source(4, '\x00');    ///< Placeholder source IPv4 address (0.0.0.0).
    }

    /**
     * @namespace IPv6
     * @brief Contains IPv6 address-related constants.
     */
    namespace IPv6
    {
        inline const ByteString source(16, '\x00'); ///< Placeholder for source IPv6 address (::);
        inline const ByteString multicast("\xFF\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16); ///< Multicast address for all neighbors.
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
            inline const ByteString discover("\x01", 1); ///< DHCP Discover message type.
            inline const ByteString offer("\x02", 1);    ///< DHCP Offer message type.
            inline const ByteString request("\x03", 1);  ///< DHCP Request message type.
            inline const ByteString decline("\x04", 1);  ///< DHCP Decline message type.
            inline const ByteString ack("\x05", 1);      ///< DHCP Acknowledgment message type.
            inline const ByteString nak("\x06", 1);      ///< DHCP Negative Acknowledgment message type.
            inline const ByteString release("\x07", 1);  ///< DHCP Release message type
            inline const ByteString inform("\x08", 1);   ///< DHCP Inform message type
            inline const ByteString forceRenew("\x09", 1);//< DHCP Force Renew message type.
        }

        /**
         * @namespace Option
         * @brief Contains DHCP option codes.
         */
        namespace Option
        {
            inline const ByteString mask("\x01", 1);                   ///< DHCP Option for Subnet Mask.
            inline const ByteString broadcast("\x1c", 1);              ///< DHCP Option for Broadcast Address.
            inline const ByteString timeOffset("\x02", 1);             ///< DHCP Option for Time Offset.
            inline const ByteString router("\x03", 1);                 ///< DHCP Option for Router.
            inline const ByteString domainName("\x0f", 1);             ///< DHCP Option for Domain Name.
            inline const ByteString domainServer("\x06", 1);           ///< DHCP Option for Domain Name Server.
            inline const ByteString domainSearch("\x77", 1);           ///< DHCP Option for Domain Search.
            inline const ByteString netbiosNameServer("\x2c", 1);      ///< DHCP Option for NetBIOS Name Server.
            inline const ByteString mtu("\x1a", 1);                    ///< DHCP Option for MTU.
            inline const ByteString classlessStateRoute("\x79", 1);    ///< DHCP Option for Classless Static Route.
            inline const ByteString ntp("\x2a", 1);                    ///< DHCP Option for NTP Servers.
            inline const ByteString type("\x35", 1);                   ///< DHCP Option for Message Type.
            inline const ByteString hostname("\x0c", 1);               ///< DHCP Option for Hostname.
            inline const ByteString clientID("\x3d", 1);               ///< DHCP Option for Client Identifier.
            inline const ByteString serverIdentifier("\x36", 1);       ///< DHCP Option for Server Identifier.
            inline const ByteString leaseTime("\x33", 1);              ///< DHCP Option for Lease Time.
            inline const ByteString renewalTime("\x3a", 1);            ///< DHCP Option for Renewal Time.
            inline const ByteString rebindingTime("\x3b", 1);          ///< DHCP Option for Rebinding Time.
            inline const ByteString requestIP("\x32", 1);              ///< DHCP Option for Requested IP Address.
            inline const ByteString requestList("\x37", 1);            ///< DHCP Option for Parameter Request List.
            inline const ByteString maxSize("\x39", 1);                ///< DHCP Option for Maximum DHCP Message Size.
            inline const ByteString tftpServer("\x42", 1);             ///< DHCP Option for TFTP server.
            inline const ByteString bootfile("\x43", 1);               ///< DHCP Option for Bootfile.
            inline const ByteString staticRoute("\x21", 1);            ///< DHCP Option for obtaining static routes.
            inline const ByteString vendorSpecific("\x2b", 1);         ///< DHCP Option for vendor specific information.
            inline const ByteString authentication("\x5a", 1);         ///< DHCP Option for Authentication.
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

        inline const ByteString clientHardwareAddressPadding(10, '\x00'); ///< Padding for Client Hardware Address.
        inline const ByteString serverHostName(64, '\x00');               ///< Server Hostname.
        inline const ByteString bootfile(128, '\x00');                    ///< Bootfile Name.
        inline const ByteString endPadding(25, '\x00');                   ///< Padding after DHCP options.
        inline const ByteString end(1, '\xff');                           ///< DHCP Option End Marker.
        inline const ByteString magicCookie("\x63\x82\x53\x63", 4);       ///< DHCP Magic Cookie.    
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
            inline const ByteString solicit("\x01", 1);             ///< DHCPv6 Solicit message type.
            inline const ByteString advertise("\x02", 1);           ///< DHCPv6 Advertise message type.
            inline const ByteString request("\x03", 1);             ///< DHCPv6 Request message type.
            inline const ByteString confirm("\x04", 1);             ///< DHCPv6 Confirm message type.
            inline const ByteString renew("\x05", 1);               ///< DHCPv6 Renew message type.
            inline const ByteString rebind("\x06", 1);              ///< DHCPv6 Rebind message type.
            inline const ByteString reply("\x07", 1);               ///< DHCPv6 Reply message type.
            inline const ByteString release("\x08", 1);             ///< DHCPv6 Release message type.
            inline const ByteString decline("\x09", 1);             ///< DHCPv6 Decline message type.
            inline const ByteString reconfigure("\x0A", 1);         ///< DHCPv6 Reconfigure message type.
            inline const ByteString informationRequest("\x0B", 1);  ///< DHCPv6 Information request message type.
            inline const ByteString relayForward("\x0C", 1);        ///< DHCPv6 Relay forward message type.
            inline const ByteString relayReply("\x0D", 1);          ///< DHCPv6 Relay reply message type.
            inline const ByteString leaseQuery("\x0E", 1);          ///< DHCPv6 Lease Query message type.
            inline const ByteString leaseQueryReply("\x0F", 1);     ///< DHCPv6 Lease Query reply message type.
            inline const ByteString leaseQueryDone("\x10", 1);      ///< DHCPv6 Lease Query done message type.
            inline const ByteString leaseQueryData("\x11", 1);      ///< DHCPv6 Lease Query data message type.
            inline const ByteString reocnfigureRequest("\x12", 1);  ///< DHCPv6 Reconfigure Request message type.
            inline const ByteString reconfigureReply("\x13", 1);    ///< DHCPv6 Reconfigure Reply message type.
        }

        /**
         * @namespace Options
         * @brief Contains DHCPv6 option codes.
         */
        namespace Options
        {
            inline const ByteString clientID("\x00\x01", 2);            ///< OPTION_CLIENTID (1)
            inline const ByteString serverID("\x00\x02", 2);            ///< OPTION_SERVERID (2)
            inline const ByteString IA_NA("\x00\x03", 2);               ///< OPTION_IA_NA (3)
            inline const ByteString IA_TA("\x00\x04", 2);               ///< OPTION_IA_TA (4)
            inline const ByteString IAAddr("\x00\x05", 2);              ///< OPTION_IAADDR (5)
            inline const ByteString optionRequest("\x00\x06", 2);       ///< OPTION_ORO (6)
            inline const ByteString preference("\x00\x07", 2);          ///< OPTION_PREFERENCE (7)
            inline const ByteString elapsedTime("\x00\x08", 2);         ///< OPTION_ELAPSED_TIME (8)
            inline const ByteString relayMsg("\x00\x09", 2);            ///< OPTION_RELAY_MSG (9)
            inline const ByteString auth("\x00\x0B", 2);                ///< OPTION_AUTH (11)
            inline const ByteString unicast("\x00\x0C", 2);             ///< OPTION_UNICAST (12)
            inline const ByteString statusCode("\x00\x0D", 2);          ///< OPTION_STATUS_CODE (13)
            inline const ByteString rapidCommit("\x00\x0E", 2);         ///< OPTION_RAPID_COMMIT (14)
            inline const ByteString reconfigureMessage("\x00\x13", 2);  ///< OPTION_RECONF_MSG (19)
            inline const ByteString reconfAccept("\x00\x14", 2);        ///< OPTION_RECONF_ACCEPT (20)
            inline const ByteString dnsServer("\x00\x17", 2);           ///< OPTION_DNS_SERVERS (23)
            inline const ByteString domainSearch("\x00\x18", 2);        ///< OPTION_DOMAIN_LIST (24)
            inline const ByteString IA_PD("\x00\x19", 2);               ///< OPTION_IA_PD (25)
            inline const ByteString fqdn("\x00\x27", 2);                ///< OPTION_CLIENT_FQDN (39)
            inline const ByteString IA_Prefix("\x00\x1A", 2);           ///< OPTION_IAPREFIX (26)
            inline const ByteString infoRefreshTime("\x00\x20", 2);     ///< OPTION_INFO_REFRESH (32)
            inline const ByteString ntpServer("\x00\x38", 2);           ///< OPTION_NTP_SERVER (56)
            inline const ByteString solMaxRt("\x00\x52", 2);            ///< OPTION_SOL_MAX_RT (82)
            inline const ByteString infMaxRt("\x00\x53", 2);            ///< OPTION_INF_MAX_RT (83)

            inline const ByteString remoteID("\x00\x25", 2);            ///< OPTION_REMOTE_ID (37)
            inline const ByteString interfaceID("\x00\x12", 2);         ///< OPTION_INTERFACE_ID (18)

            inline const ByteString leaseQuery("\x00\x2c", 2);          ///< OPTION_LQ_QUERY (44)
            inline const ByteString leaseClientData("\x00\x2d", 2);     ///< OPTION_QLIENT_DATA (45)
            inline const ByteString leaseQueryData("\x00\x2e", 2);      ///< OPTION_LQ_RELAY_DATA (46)

            inline const ByteString queryByAddr("\x00\x01", 2);         ///< QUERY_BY_ADDRESS (1)
            inline const ByteString queryByClientID("\x00\x02", 2);     ///< QUERY_BY_CLIENTID (2)

        }

        /**
         * @namespace Status
         * @brief Contains DHCPv6 status codes.
         */
        namespace Status 
        {
            inline const ByteString success("\x00\x00", 2);             ///< Status code for success.
            inline const ByteString unspecFail("\x00\x01", 2);          ///< Status code for unspecified failure.
            inline const ByteString noAddrsAvail("\x00\x02", 2);        ///< Status code for no address available.
            inline const ByteString noBinding("\x00\x03", 2);           ///< Status code for no binding available.
            inline const ByteString notOnLink("\x00\x04", 2);           ///< Status code for not on link.
            inline const ByteString useMulticast("\x00\x05", 2);        ///< Status code for using multicast.
            inline const ByteString noPrefixAvail("\x00\x06", 2);       ///< Status code for no prefix available.
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
            inline const ByteString update("\x01", 1);   ///< EIGRP Update message type.
            inline const ByteString request("\x02", 1);  ///< EIGRP Request message type.
            inline const ByteString query("\x03", 1);    ///< EIGRP Query message type.
            inline const ByteString reply("\x04", 1);    ///< EIGRP Reply message type.
            inline const ByteString hello("\x05", 1);    ///< EIGRP Hello message type.
            inline const ByteString siaQuery("\xa0", 1); ///< Eigrp SIAQuery message type.
            inline const ByteString siaReply("\xa1", 1); ///< Eigrp SIAReply message type
        }

        /**
         * @namespace Option
         * @brief Contains EIGRP option codes.
         */
        namespace Option
        {
            inline const ByteString parameter("\x00\x01", 2);             ///< EIGRP Option for Parameter.
            inline const ByteString version("\x00\x04", 2);               ///< EIGRP Option for Version.
            inline const ByteString sequence("\x00\x03", 2);              ///< EIGRP Option for Sequence Number.
            inline const ByteString multicastSequence("\x00\x05", 2);     ///< EIGRP Option for Multicast Sequence.
            inline const ByteString internalRoute("\x01\x02", 2);         ///< EIGRP Option for Internal Route.
            inline const ByteString externalRoute("\x01\x03", 2);         ///< EIGRP Option for External Route.
            inline const ByteString internalRouteV6("\x04\x02", 2);       ///< EIGRP Option for Internal Route IPv6.
            inline const ByteString externalRouteV6("\x04\x03", 2);       ///< EIGRP Option for External Route IPv6.
            inline const ByteString stub("\x00\x06", 2);                  ///< EIGRP Option for Stub.
            inline const ByteString authentication("\x00\x02", 2);        ///< EIGRP Option for Authentication.
        }

        /**
         * @namespace Version
         * @brief Contains EIGRP version constants.
         */
        namespace Version
        {
            inline const ByteString release("\x0c\x04", 2);  ///< EIGRP Version Release.
            inline const ByteString tls("\x01\x02", 2);      ///< EIGRP Version TLS.
        }

        /**
         * @namespace ExternalProtocol
         * @brief Contains EIGRP external protocol constants.
         */
        namespace ExternalProtocol 
        {
            inline const ByteString igrp("\x01", 1);          ///< EIGRP External Protocol IGRP.
            inline const ByteString eigrp("\x02", 1);         ///< EIGRP External Protocol EIGRP.
            inline const ByteString staticRoute("\x03", 1);   ///< EIGRP External Protocol Static Route.
            inline const ByteString rip("\x04", 1);           ///< EIGRP External Protocol RIP.
            inline const ByteString hello("\x05", 1);         ///< EIGRP External Protocol Hello.
            inline const ByteString ospf("\x06", 1);          ///< EIGRP External Protocol OSPF.
            inline const ByteString isis("\x07", 1);          ///< EIGRP External Protocol ISIS.
            inline const ByteString bgp("\x09", 1);           ///< EIGRP External Protocol BGP.
            inline const ByteString connected("\x0b", 1);     ///< EIGRP External Protocol Connected.
        }

        /**
         * @namespace DestinationAssignmentEncoding
         * @brief Contains EIGRP Destination Assignment Encoding constants.
         */
        namespace DestinationAssignmentEncoding 
        {
            inline const ByteString ipv4("\x01", 1);              ///< EIGRP Destination Assignment Encoding IPv4.
            inline const ByteString ipv6("\x02", 1);              ///< EIGRP Destination Assignment Encoding IPv6.
            inline const ByteString commonService("\x40\x00", 2); ///< EIGRP Destination Assignment Encoding Common Service.
            inline const ByteString ipv4Family("\x40\x01", 2);    ///< EIGRP Destination Assignment Encoding IPv4 Family.
            inline const ByteString ipv6Famil("\x40\x02", 2);     ///< EIGRP Destination Assignment Encoding IPv6 Family.
        }

        /**
         * @namespace CommunityAttribute
         * @brief Contains EIGRP Community Attribute constants.
         */
        namespace CommunityAttribute 
        {
            inline const ByteString EXTCOMM_EIGRP("\x00", 1);       ///< EIGRP Community Attribute for EIGRP.
            inline const ByteString EXTCOMM_DAD("\x01", 1);         ///< EIGRP Community Attribute for DAD.
            inline const ByteString EXTCOMM_VRHB("\x02", 1);        ///< EIGRP Community Attribute for VRHB.
            inline const ByteString EXTCOMM_SRLM("\x03", 1);        ///< EIGRP Community Attribute for SRLM.
            inline const ByteString EXTCOMM_SAR("\x04", 1);         ///< EIGRP Community Attribute for SAR.
            inline const ByteString EXTCOMM_RPM("\x05", 1);         ///< EIGRP Community Attribute for RPM.
            inline const ByteString EXTCOMM_VRR("\x06", 1);         ///< EIGRP Community Attribute for VRR.
        }

        /**
         * @namespace Dampening
         * @brief Contains Eigrp Dampening constants
         */
        namespace Dampening
        {
            inline const uint32_t flapPenalty = 1000; ///< Eigrp dampening flap penalty.
            inline const uint32_t supressThreshold = 2000; ///< Supression threshold from penalty.
            inline const uint32_t reuseThreshold = 750; ///< Penalty needed to be unsupressed.
            inline const uint32_t decayInterval = 5; ///< Penalty decays every 5 seconds.
            inline const uint32_t halflifeTime = 15; ///< Penalty is halfed every 15 seconds.
            inline const uint32_t maxSuppressTime = 10; ///< Maximum time a route can be supressed.
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
            inline const ByteString address("\xe0\x00\x00\x0a", 4); ///< EIGRP Multicast IPv4 Address.
            inline const ByteString addressv6("\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a", 16); ///< EIGRP Multicast IPv6 Address.
            inline const ByteString mac("\x01\x00\x5e\x00\x00\x0a", 6); ///< EIGRP Multicast MAC Address.
            inline const ByteString macv6("\x33\x33\x00\x00\x00\x0a", 6); ///< EIGRP Multicast MAC Address for IPv6.
        }

        /**
         * @namespace ICMPv6
         * @brief Contains multicast address constants for ICMPv6
         */
        namespace ICMPv6
        {
            inline const ByteString solicitationAddress("\xFF\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\xFF", 13);
            inline const ByteString allRouters("\xFF\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02", 16);
            
        }

        /**
         * @namespace DHCP
         * @brief Contains DHCP multicast address and constants.
         */
        namespace Dhcp
        {
            inline const ByteString clientToServerv6("\xff\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02", 16);
            inline const ByteString relayToServerv6("\xff\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x03", 16);
            inline const ByteString serverToAllv6("\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);
        }
    }
}

// ------------------------- Structure Definitions -------------------------

// Layer 2 Variants
using Layer2Variant = std::variant<EthernetHeader>;

// Layer 2.5 Variants
using Layer2_5Variant = std::variant<ArpHeader, MplsHeader, VlanHeader>;

// Layer 3 Variants
using Layer3Variant = std::variant<IPv4Header, IPv6Header, GreHeade, AhHeader, EspHeader, IcmpHeader, IcmpV6Header, IgmpHeader>;

// Layer 4 Variants
using Layer4Variant = std::variant<TcpHeader, UdpHeader, EigrpHeader>;

// Layer 5 Variants
using Layer5Variant = std::variant<DhcpHeader, Dhcpv6Header, Dhcpv6RelayHeader>;

// All Packet Headers Varient
using AllPacketHeaders = std::variant<Layer2Variant, Layer2_5Variant, Layer3Variant, Layer4Variant, Layer5Variant>;

/**
 * @struct PacketInfo
 * @brief Aggregates packet information across different protocol layers.
 */
struct PacketInfo
{
    std::vector<Layer2Variant> Layer2{};    ///< Vector of Layer 2 headers (e.g., Ethernet, PPP).
    std::vector<Layer2_5Variant> Layer2_5{};  ///< Vector of Layer 2.5 headers (e.g., ARP, MPLS, VLAN, LLDP).
    std::vector<Layer3Variant> Layer3{};    ///< Vector of Layer 3 headers (e.g., IPv4, IPv6, GRE, AH, ESP, ICMP, IGMP, EIGRP).
    std::vector<Layer4Variant> Layer4{};    ///< Vector of Layer 4 headers (e.g., TCP, UDP).
    std::vector<Layer5Variant> Layer5{};    ///< Vector of Layer 5 headers (e.g., DHCP, DHCPv6).
};

/**
 * @brief Checks if a given `std::any` object holds a specific type.
 *
 * This template function compares the type of the provided `std::any` object with the specified type `T`.
 *
 * @tparam T The type to check against.
 * @param a The `std::any` object to inspect.
 * @return `true` if `a` holds a value of type `T`; `false` otherwise.
 */
template <typename T>
bool is_type(const std::any &a)
{
    return std::any_cast<T>(&a) != nullptr;
}

/**
 * @brief Flattens a PacketInfo object to a list of all headers in the packet
 */
inline std::vector<AllPacketHeaders> flattenHeaders(const PacketInfo& pkt)
{
    std::vector<AllPacketHeaders> result;

    // Helper lamda to append header from each layer to the result
    auto append = [&result](const auto& layerVac)
    {
        for (const auto& v : layerVac)
        {
            std::visit([&](const auto& header)
            {
                result.emplace_back(header);
            }, v);
        }
    };

    append(pkt.Layer2);
    append(pkt.Layer2_5);
    append(pkt.Layer3);
    append(pkt.Layer4);
    append(pkt.Layer5);

    return result;
}

#endif // PACKET_STRUCTURE_H
