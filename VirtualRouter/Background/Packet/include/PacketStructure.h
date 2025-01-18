// PacketStructure.h

#ifndef PACKET_STRUCTURE_H
#define PACKET_STRUCTURE_H

#include <any>
#include <typeinfo>
#include <Functions.h>
#include <ByteString.hpp>
#include <Checksums.h>
#include <variant>
#include <Profiler.hpp>

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
        inline const std::string arp("\x08\x06", 2);    ///< EtherType for ARP
        inline const std::string ipv4("\x08\x00", 2);   ///< EtherType for IPv4
        inline const std::string ipv6("\x86\xdd", 2);   ///< EtherType for IPv6
        inline const std::string mpls("\x88\x47", 2);   ///< EtherType for MPLS
        inline const std::string vlan("\x81\x00", 2);   ///< EtherType for VLAN
        inline const std::string lldp("\x88\xcc", 2);   ///< EtherType for LLDP
    }
    
    /**
     * @namespace Arp
     * @brief Contains ARP-related constants.
     */
    namespace Arp
    {
        inline const std::string ethernet("\x00\x01", 2);   ///< Hardware type for Ethernet
        inline const std::string ipv4("\x08\x00", 2);       ///< Hardware type for IPv4

        /**
         * @namespace Opcode
         * @brief Contains ARP opcode constants.
         */
        namespace Opcode
        {
            inline const std::string request("\x00\x01", 2);           ///< ARP Request.
            inline const std::string reply("\x00\x02", 2);             ///< ARP Reply.
            inline const std::string reverseRequest("\x00\x03", 2);    ///< Reverse ARP Request.
            inline const std::string reverseReply("\x00\x04", 2);      ///< Reverse ARP Reply.
            inline const std::string dynamicRequest("\x00\x05", 2);    ///< Dynamic ARP Request.
            inline const std::string dynamicReply("\x00\x06", 2);      ///< Dynamic ARP Reply.
            inline const std::string dynamicError("\x00\x07", 2);      ///< Dynamic ARP Error.
            inline const std::string inverseRequest("\x00\x08", 2);    ///< Inverse ARP Request.
            inline const std::string inverseReply("\x00\x09", 2);      ///< Inverse ARP Reply.
            inline const std::string nak("\x00\x0a", 2);               ///< ARP NAK (Negative Acknowledgment).
        }
    }

    /**
     * @namespace IP
     * @brief Contains IP protocol number constants.
     */
    namespace IP
    {
        // Existing Protocols
        inline const std::string esp("\x32", 1);      ///< IP protocol number for ESP (50).
        inline const std::string ah("\x33", 1);       ///< IP protocol number for AH (51).
        inline const std::string gre("\x2f", 1);      ///< IP protocol number for GRE (47).
        inline const std::string igmp("\x02", 1);     ///< IP protocol number for IGMP (2).
        inline const std::string none("\x3b", 1);     ///< IP protocol number for None (59).
        inline const std::string tcp("\x06", 1);      ///< IP protocol number for TCP (6).
        inline const std::string udp("\x11", 1);      ///< IP protocol number for UDP (17).
        inline const std::string icmpv4("\x01", 1);   ///< IP protocol number for ICMPv4 (1).
        inline const std::string icmpv6("\x3a", 1);   ///< IP protocol number for ICMPv6 (58).
        inline const std::string sctp("\x84", 1);     ///< IP protocol number for SCTP (132).
        inline const std::string eigrp("\x58", 1);    ///< IP protocol number for EIGRP (88).
        inline const std::string ospf("\x59", 1);     ///< IP protocol number for OSPF (89).
        inline const std::string pim("\x67", 1);      ///< IP protocol number for PIM (103).
        inline const std::string rsvp("\x2e", 1);     ///< IP protocol number for RSVP (46). Corrected from \x2f to \x2e.

        // Additional Protocols
        inline const std::string hopopt("\x00", 1);       ///< IP protocol number for HOPOPT (0).
        inline const std::string ggp("\x03", 1);          ///< IP protocol number for GGP (3).
        inline const std::string ipv4("\x04", 1);         ///< IP protocol number for IPv4 (4).
        inline const std::string st("\x05", 1);           ///< IP protocol number for ST (5).
        inline const std::string cbt("\x07", 1);          ///< IP protocol number for CBT (7).
        inline const std::string egp("\x08", 1);          ///< IP protocol number for EGP (8).
        inline const std::string igp_v2("\x09", 1);       ///< IP protocol number for IGP (9).
        inline const std::string bbn_rcc_mon("\x0a", 1);  ///< IP protocol number for BBN_RCC_MON (10).
        inline const std::string nvp_ii("\x0b", 1);        ///< IP protocol number for NVP-II (11).
        inline const std::string pup("\x0c", 1);           ///< IP protocol number for PUP (12).
        inline const std::string argus("\x0d", 1);         ///< IP protocol number for ARGUS (13).
        inline const std::string emcon("\x0e", 1);         ///< IP protocol number for EMCON (14).
        inline const std::string xnet("\x0f", 1);          ///< IP protocol number for XNET (15).
        inline const std::string chaos("\x10", 1);         ///< IP protocol number for CHAOS (16).
        inline const std::string mux("\x12", 1);           ///< IP protocol number for MUX (18).
        inline const std::string dcn_meas("\x13", 1);      ///< IP protocol number for DCN_MEAS (19).
        inline const std::string hmp("\x14", 1);           ///< IP protocol number for HMP (20).
        inline const std::string prm("\x15", 1);           ///< IP protocol number for PRM (21).
        inline const std::string xns_idp("\x16", 1);       ///< IP protocol number for XNS_IDP (22).
        inline const std::string trunk_1("\x17", 1);       ///< IP protocol number for TRUNK-1 (23).
        inline const std::string trunk_2("\x18", 1);       ///< IP protocol number for TRUNK-2 (24).
        inline const std::string leaf_1("\x19", 1);        ///< IP protocol number for LEAF-1 (25).
        inline const std::string leaf_2("\x1a", 1);        ///< IP protocol number for LEAF-2 (26).
        inline const std::string rdp("\x1b", 1);           ///< IP protocol number for RDP (27).
        inline const std::string irtp("\x1c", 1);          ///< IP protocol number for IRTP (28).
        inline const std::string iso_tp4("\x1d", 1);       ///< IP protocol number for ISO_TP4 (29).
        inline const std::string netblt("\x1e", 1);        ///< IP protocol number for NETBLT (30).
        inline const std::string mfe_nsp("\x1f", 1);       ///< IP protocol number for MFE_NSP (31).
        inline const std::string merit_inp("\x20", 1);     ///< IP protocol number for MERIT_INP (32).
        inline const std::string dccp("\x21", 1);          ///< IP protocol number for DCCP (33).
        inline const std::string ipcomp("\x22", 1);        ///< IP protocol number for IPCOMP (34).
        inline const std::string eigrp_v6("\x58", 1);      ///< IP protocol number for EIGRP (88).
        inline const std::string ospf_v3("\x59", 1);       ///< IP protocol number for OSPF (89).
        inline const std::string pim_sm("\x67", 1);        ///< IP protocol number for PIM-SM (103).
        inline const std::string l2tp("\x4e", 1);          ///< IP protocol number for L2TP (78).
        inline const std::string mpls_in_ip("\x2b", 1);    ///< IP protocol number for MPLS-in-IP (43).
        inline const std::string vxlan("\xb7", 1);         ///< IP protocol number for VXLAN (183).
        inline const std::string dvrp("\x5e", 1);          ///< IP protocol number for DVRP (94).
        inline const std::string lmtp("\x46", 1);          ///< IP protocol number for LMTP (70).
        inline const std::string encap("\x8e", 1);         ///< IP protocol number for ENCAPSULATION (142).
        inline const std::string ipv6("\x29", 1);          ///< IP protocol number for IPv6 (41).
        inline const std::string pim_dm("\x64", 1);        ///< IP protocol number for PIM-DM (100).
        inline const std::string eigrp_v5("\x8a", 1);      ///< IP protocol number for EIGRP for IPv6 (138).
        inline const std::string rsvp_te("\x73", 1);       ///< IP protocol number for RSVP-TE (115).
        inline const std::string mpls("\x2b", 1);          ///< IP protocol number for MPLS (43). Note: MPLS has multiple entries.
        inline const std::string pppoe_discovery("\x11", 1); ///< IP protocol number for PPPoE Discovery (17).
        inline const std::string pppoe_session("\x11", 1);   ///< IP protocol number for PPPoE Session (17).
        inline const std::string ppp_echo("\x01", 1);        ///< IP protocol number for PPP Echo (1).
        inline const std::string ppp_ipcp("\x21", 1);        ///< IP protocol number for PPP IPCP (33).
        inline const std::string ppp_ipv6cp("\x57", 1);      ///< IP protocol number for PPP IPv6CP (87).
        inline const std::string eap("\x88", 1);             ///< IP protocol number for EAP (136).
        inline const std::string lisp_control("\x8f", 1);    ///< IP protocol number for LISP Control (143).
        inline const std::string mobileregistration_protocol("\x8d", 1); ///< IP protocol number for Mobile Registration Protocol (141).
    }

    /**
     * @namespace Udp
     * @brief Contains UDP port constants related to DHCP.
     */
    namespace Udp
    {
        /**
         * @namespace Dhcp
         * @brief Contains DHCP-related UDP port constants.
         */
        namespace Dhcp
        {
            inline const std::string source("\x00\x44", 2);      ///< UDP source port for DHCP.
            inline const std::string destination("\x00\x43", 2); ///< UDP destination port for DHCP.
        }
    }

    /**
     * @namespace Gre
     * @brief Contains GRE-related constants.
     */
    namespace Gre
    {
        inline const std::string ppp("\x88\x0b", 2); ///< GRE protocol type for PPP.
    }

    /**
     * @namespace Ah
     * @brief Contains AH-related constants.
     */
    namespace Ah
    {
        inline const std::string esp("\x32", 1); ///< AH protocol number for ESP.
    }

    /**
     * @namespace Mac
     * @brief Contains MAC address-related constants.
     */
    namespace Mac
    {
        inline const std::string broadcast(6, '\xff'); ///< Broadcast MAC address (FF:FF:FF:FF:FF:FF).
        inline const std::string source(6, '\x00');    ///< Placeholder source MAC address (00:00:00:00:00:00).
    }

    /**
     * @namespace IPv4
     * @brief Contains IPv4 address-related constants.
     */
    namespace IPv4
    {
        inline const std::string broadcast(4, '\xff'); ///< Broadcast IPv4 address (255.255.255.255).
        inline const std::string source(4, '\x00');    ///< Placeholder source IPv4 address (0.0.0.0).
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
            inline const std::string discover("\x01", 1); ///< DHCP Discover message type.
            inline const std::string offer("\x02", 1);    ///< DHCP Offer message type.
            inline const std::string request("\x03", 1);  ///< DHCP Request message type.
            inline const std::string ack("\x05", 1);      ///< DHCP Acknowledgment message type.
            inline const std::string nak("\x06", 1);      ///< DHCP Negative Acknowledgment message type.
        }

        /**
         * @namespace Option
         * @brief Contains DHCP option codes.
         */
        namespace Option
        {
            inline const std::string mask("\x01", 1);                   ///< DHCP Option for Subnet Mask.
            inline const std::string broadcast("\x1c", 1);              ///< DHCP Option for Broadcast Address.
            inline const std::string timeOffset("\x02", 1);             ///< DHCP Option for Time Offset.
            inline const std::string router("\x03", 1);                 ///< DHCP Option for Router.
            inline const std::string domainName("\x0f", 1);             ///< DHCP Option for Domain Name.
            inline const std::string domainServer("\x06", 1);           ///< DHCP Option for Domain Name Server.
            inline const std::string domainSearch("\x77", 1);           ///< DHCP Option for Domain Search.
            inline const std::string netbiosNameServer("\x2c", 1);      ///< DHCP Option for NetBIOS Name Server.
            inline const std::string netbiosScope("\x2c", 1);           ///< DHCP Option for NetBIOS Scope.
            inline const std::string mtu("\x1a", 1);                    ///< DHCP Option for MTU.
            inline const std::string classlessStateRoute("\x79", 1);    ///< DHCP Option for Classless Static Route.
            inline const std::string ntp("\x2a", 1);                    ///< DHCP Option for NTP Servers.
            inline const std::string type("\x35", 1);                   ///< DHCP Option for Message Type.
            inline const std::string hostname("\x0c", 1);               ///< DHCP Option for Hostname.
            inline const std::string clientID("\x3d", 1);               ///< DHCP Option for Client Identifier.
            inline const std::string serverIdentifier("\x36", 1);       ///< DHCP Option for Server Identifier.
            inline const std::string leaseTime("\x33", 1);              ///< DHCP Option for Lease Time.
            inline const std::string renewalTime("\x3a", 1);            ///< DHCP Option for Renewal Time.
            inline const std::string rebindingTime("\x3b", 1);          ///< DHCP Option for Rebinding Time.
            inline const std::string requestIP("\x32", 1);              ///< DHCP Option for Requested IP Address.
            inline const std::string requestList("\x37", 1);            ///< DHCP Option for Parameter Request List.
            inline const std::string maxSize("\x39", 1);                ///< DHCP Option for Maximum DHCP Message Size.
        }

        inline const std::string clientHardwareAddressPadding(10, '\x00'); ///< Padding for Client Hardware Address.
        inline const std::string serverHostName(64, '\x00');               ///< Server Hostname.
        inline const std::string bootfile(128, '\x00');                    ///< Bootfile Name.
        inline const std::string endPadding(25, '\x00');                   ///< Padding after DHCP options.
        inline const std::string end(1, '\xff');                           ///< DHCP Option End Marker.
        inline const std::string magicCookie("\x63\x82\x53\x63", 4);       ///< DHCP Magic Cookie.    
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
            inline const std::string update("\x01", 1);   ///< EIGRP Update message type.
            inline const std::string request("\x02", 1);  ///< EIGRP Request message type.
            inline const std::string query("\x03", 1);    ///< EIGRP Query message type.
            inline const std::string reply("\x04", 1);    ///< EIGRP Reply message type.
            inline const std::string hello("\x05", 1);    ///< EIGRP Hello message type.
        }

        /**
         * @namespace Option
         * @brief Contains EIGRP option codes.
         */
        namespace Option
        {
            inline const std::string parameter("\x00\x01", 2);             ///< EIGRP Option for Parameter.
            inline const std::string version("\x00\x04", 2);               ///< EIGRP Option for Version.
            inline const std::string sequence("\x00\x03", 2);              ///< EIGRP Option for Sequence Number.
            inline const std::string multicastSequence("\x00\x05", 2);     ///< EIGRP Option for Multicast Sequence.
            inline const std::string internalRoute("\x01\x02", 2);         ///< EIGRP Option for Internal Route.
            inline const std::string externalRoute("\x01\x03", 2);         ///< EIGRP Option for External Route.
            inline const std::string internalRouteV6("\x04\x02", 2);       ///< EIGRP Option for Internal Route IPv6.
            inline const std::string externalRouteV6("\x04\x03", 2);       ///< EIGRP Option for External Route IPv6.
            inline const std::string stub("\x00\x06", 2);                  ///< EIGRP Option for Stub.
            inline const std::string authentication("\x00\x02", 2);        ///< EIGRP Option for Authentication.
        }

        /**
         * @namespace Version
         * @brief Contains EIGRP version constants.
         */
        namespace Version
        {
            inline const std::string release("\x0c\x04", 2);  ///< EIGRP Version Release.
            inline const std::string tls("\x01\x02", 2);      ///< EIGRP Version TLS.
        }

        /**
         * @namespace ExternalProtocol
         * @brief Contains EIGRP external protocol constants.
         */
        namespace ExternalProtocol 
        {
            inline const std::string igrp("\x01", 1);          ///< EIGRP External Protocol IGRP.
            inline const std::string eigrp("\x02", 1);         ///< EIGRP External Protocol EIGRP.
            inline const std::string staticRoute("\x03", 1);   ///< EIGRP External Protocol Static Route.
            inline const std::string rip("\x04", 1);           ///< EIGRP External Protocol RIP.
            inline const std::string hello("\x05", 1);         ///< EIGRP External Protocol Hello.
            inline const std::string ospf("\x06", 1);          ///< EIGRP External Protocol OSPF.
            inline const std::string isis("\x07", 1);          ///< EIGRP External Protocol ISIS.
            inline const std::string egp("\x08", 1);           ///< EIGRP External Protocol EGP.
            inline const std::string bgp("\x09", 1);           ///< EIGRP External Protocol BGP.
            inline const std::string idrp("\x0a", 1);          ///< EIGRP External Protocol IDRP.
            inline const std::string connected("\x0b", 1);     ///< EIGRP External Protocol Connected.
        }

        /**
         * @namespace DestinationAssignmentEncoding
         * @brief Contains EIGRP Destination Assignment Encoding constants.
         */
        namespace DestinationAssignmentEncoding 
        {
            inline const std::string ipv4("\x01", 1);              ///< EIGRP Destination Assignment Encoding IPv4.
            inline const std::string ipv6("\x02", 1);              ///< EIGRP Destination Assignment Encoding IPv6.
            inline const std::string commonService("\x40\x00", 2); ///< EIGRP Destination Assignment Encoding Common Service.
            inline const std::string ipv4Family("\x40\x01", 2);    ///< EIGRP Destination Assignment Encoding IPv4 Family.
            inline const std::string ipv6Famil("\x40\x02", 2);     ///< EIGRP Destination Assignment Encoding IPv6 Family.
        }

        /**
         * @namespace CommunityAttribute
         * @brief Contains EIGRP Community Attribute constants.
         */
        namespace CommunityAttribute 
        {
            inline const std::string EXTCOMM_EIGRP("\x00", 1);       ///< EIGRP Community Attribute for EIGRP.
            inline const std::string EXTCOMM_DAD("\x01", 1);         ///< EIGRP Community Attribute for DAD.
            inline const std::string EXTCOMM_VRHB("\x02", 1);        ///< EIGRP Community Attribute for VRHB.
            inline const std::string EXTCOMM_SRLM("\x03", 1);        ///< EIGRP Community Attribute for SRLM.
            inline const std::string EXTCOMM_SAR("\x04", 1);         ///< EIGRP Community Attribute for SAR.
            inline const std::string EXTCOMM_RPM("\x05", 1);         ///< EIGRP Community Attribute for RPM.
            inline const std::string EXTCOMM_VRR("\x06", 1);         ///< EIGRP Community Attribute for VRR.
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
            inline const std::string address("\xe0\x00\x00\x0a", 4); ///< EIGRP Multicast IPv4 Address.
            inline const std::string addressv6("\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a", 16); ///< EIGRP Multicast IPv6 Address.
            inline const std::string mac("\x01\x00\x5e\x00\x00\x0a", 6); ///< EIGRP Multicast MAC Address.
            inline const std::string macv6("\x33\x33\x00\x00\x00\x0a", 6); ///< EIGRP Multicast MAC Address for IPv6.
        }
    }
}

// ------------------------- Structure Definitions -------------------------

static bool validateSize(size_t& beginning, size_t length, const ByteString& packet)
{
    return (beginning + length <= packet.toString().size());
}

/**
 * @struct EthernetHeader
 * @brief Represents an Ethernet frame header.
 */
struct EthernetHeader
{
    ByteString sourceMac{};         ///< Source MAC address
    ByteString destinationMac{};    ///< Destination MAC address
    ByteString type{};              ///< Next header type

    const std::optional<ByteString> encapsulate() const
    {
        //Profiler::getInstance().notify("ethernet encap start");
        ByteString ethernetString;
        ethernetString += destinationMac.size() == 6 ? destinationMac : ByteString(6, '\x00');
        if (sourceMac.size() != 6 || type.size() != 2) return std::nullopt;

        ethernetString.reserve(14);
        ethernetString.append(sourceMac);
        ethernetString.append(type);

        return ethernetString;
        //Profiler::getInstance().notify("ethernet encap end");
    }
    bool decapsulate(const ByteString ethernetHeader)
    {
        //Profiler::getInstance().notify("ethernet decap start");
        if (ethernetHeader.size() != 14) return false;
        destinationMac = ethernetHeader.substr(0, 6);
        sourceMac = ethernetHeader.substr(6, 6);
        type = ethernetHeader.substr(12, 2);
        //Profiler::getInstance().notify("ethernet decap end");
        return true;
    }
};

/**
 * @struct ArpHeader
 * @brief Represents an ARP (Address Resolution Protocol) header.
 */
struct ArpHeader
{
    ByteString hardwareType{};          ///< Hardware type (e.g., Ethernet).
    ByteString protocolType{};          ///< Protocol type (e.g., IPv4).
    ByteString hardwareSize{};          ///< Hardware address length.
    ByteString protocolSize{};          ///< Protocol address length.
    ByteString opcode{};                ///< ARP operation code.
    ByteString senderHardwareAddress{}; ///< Sender's hardware address.
    ByteString senderIpAddress{};        ///< Sender's IP address.
    ByteString targetHardwareAddress{}; ///< Target's hardware address.
    ByteString targetIpAddress{};        ///< Target's IP address.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString arpString;
        if (hardwareType.size() != 2 || protocolType.size() != 2 || hardwareSize.size() != 1 || protocolSize.size() != 1 ||
            opcode.size() != 2 || senderHardwareAddress.size() != 6 || senderIpAddress.size() != 4 ||
            senderHardwareAddress.size() != 6 || senderIpAddress.size() != 4) return std::nullopt;

        arpString.reserve(28);
        arpString += hardwareType;
        arpString += protocolType;
        arpString += hardwareSize;
        arpString += protocolSize;
        arpString += opcode;
        arpString += senderHardwareAddress;
        arpString += senderIpAddress;
        arpString += targetHardwareAddress;
        arpString += targetIpAddress;

        return arpString;
    }
    bool decapsulate(const ByteString arpHeader)
    {
        if (arpHeader.size() == 28)
        {
            hardwareType = arpHeader.substr(0, 2);
            protocolType = arpHeader.substr(2, 2);
            hardwareSize = arpHeader.substr(4, 1);
            protocolSize = arpHeader.substr(5, 1);
            opcode = arpHeader.substr(6, 2);
            senderHardwareAddress = arpHeader.substr(8, 6);
            senderIpAddress = arpHeader.substr(14, 4);
            targetHardwareAddress = arpHeader.substr(18, 6);
            targetIpAddress = arpHeader.substr(24, 4);
        }
        else return false;
        return true;
    }
};

/**
 * @struct MplsHeader
 * @brief Represents an MPLS (Multiprotocol Label Switching) header.
 */
struct MplsHeader
{
    ByteString label{};           ///< MPLS label.
    ByteString expBit{};          ///< Experimental bits (EXP).
    ByteString bottomLabelStack{};///< Bottom of Stack bit.
    ByteString TTL{};             ///< Time-To-Live (TTL).

    const std::optional<ByteString> encapsulate() const
    {
        ByteString mplsString;
        if (/*label.size() != 3 || TTL.size() != 1*/false) return std::nullopt;

        mplsString.reserve(8);
        mplsString += label;
        //mplsString += Functions::binToHex(mpls.expBit + mpls.bottomLabelStack);
        mplsString += TTL;
        mplsString = Functions::hexToByte(mplsString);

        return mplsString;
    }
    bool decapsulate(const ByteString mplsHeader)
    {
        if (mplsHeader.size() != 8) return false;
            
        label = mplsHeader.substr(0, 5);
        expBit = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(0, 3);
        bottomLabelStack = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(3, 1);
        TTL = mplsHeader.substr(6, 2);

        return true;
    }
};

/**
 * @struct IPv4Header
 * @brief Represents an IPv4 header.
 */
struct IPv4Header
{
    ByteString version{};        ///< Version field.
    ByteString headerLength{};   ///< Header length field.
    ByteString serviceField{};   ///< Type of Service (ToS) field.
    ByteString totalLength{};    ///< Total length of the IP packet.
    ByteString identification{}; ///< Identification field.
    ByteString TTL{};            ///< Time-To-Live (TTL) field.
    ByteString protocol{};       ///< Protocol field.
    ByteString checksum{};       ///< Header checksum.
    ByteString sourceAddress{};  ///< Source IP address.
    ByteString destinationAddress{}; ///< Destination IP address.

    /**
     * @struct FragmentFlag
     * @brief Represents fragmentation flags and offset.
     */
    struct FragmentFlag
    {
        ByteString reserved{};        ///< Reserved flag.
        ByteString fragment{};        ///< More fragments flag.
        ByteString moreFragment{};    ///< Don't Fragment flag.
        ByteString fragmentOffset{};  ///< Fragment offset.
    } fragmentFlag;

    /**
     * @struct Options
     * @brief Represents IPv4 header options.
     */
    struct Options
    {
        /**
         * @struct Type
         * @brief Represents the type of IPv4 options.
         */
        struct Type
        {
            ByteString copy{};               ///< Copy flag.
            ByteString classControl{};       ///< Class-Control flag.
            ByteString routerAlert{};        ///< Router Alert flag.
        } type;

        ByteString length{};           ///< Length of the options field.
        ByteString routerAlert{};      ///< Router Alert option value.

    } options;

    const std::optional<ByteString> encapsulate() const
    {
        //Profiler::getInstance().notify("IPv4 encap start");
        if (version.size() != 1 || headerLength.size() != 1 || serviceField.size() != 1 || totalLength.size() != 2 ||
            identification.size() != 2 || TTL.size() != 1 || protocol.size() != 1 || checksum.size() != 2 || 
            sourceAddress.size() != 4 || destinationAddress.size() != 4) return std::nullopt;

        ByteString ipv4String;
        ipv4String.reserve(20);

        ipv4String.append(Functions::hexToByte(version + headerLength));
        ipv4String.append(serviceField);
        ipv4String.append(totalLength);
        ipv4String.append(identification);
        ipv4String.append(Functions::binToByte(fragmentFlag.reserved + fragmentFlag.fragment + fragmentFlag.moreFragment + fragmentFlag.fragmentOffset));
        ipv4String.append(TTL);
        ipv4String.append(protocol);
        ipv4String.append(ByteString(2, 0x00));
        ipv4String.append(sourceAddress);
        ipv4String.append(destinationAddress);
        ipv4String.append(Functions::binToByte(options.type.copy) + options.type.classControl + options.type.routerAlert);
        ipv4String.append(options.length);
        ipv4String.append(options.routerAlert);
        
        if (options.type.copy.size() == 1 && options.type.classControl.size() == 2 && 
            options.type.routerAlert.size() == 5 && options.length.size() == 1)
        {
            ByteString typeString;
            ipv4String.append(Functions::binToByte(options.type.copy + options.type.classControl + options.type.routerAlert));
            ipv4String.append(options.length);
            ipv4String.append(options.routerAlert);
        }

        //Profiler::getInstance().notify("IPv4 encap end");
        return ipv4String;
    }
    bool decapsulate(const ByteString ipv4Header)
    {
        //Profiler::getInstance().notify("IPv4 decap start");
        if (ipv4Header.size() < 20) return false;

        ByteString ipHeader = ipv4Header.substr(0, 1).toHex();
        version = ipHeader.substr(0, 1);
        headerLength = ipHeader.substr(1, 1);
        serviceField = ipv4Header.substr(1, 1);
        totalLength = ipv4Header.substr(2, 2);
        identification = ipv4Header.substr(4, 2);
        TTL = ipv4Header.substr(8, 1);
        protocol = ipv4Header.substr(9, 1);
        checksum = ipv4Header.substr(10, 2);
        sourceAddress = ipv4Header.substr(12, 4);
        destinationAddress = ipv4Header.substr(16, 4);

        ByteString fragmentFlags = Functions::byteToBin(ipv4Header.substr(6, 2));

        fragmentFlag.reserved = fragmentFlags.substr(0, 1);
        fragmentFlag.fragment = fragmentFlags.substr(1, 1);
        fragmentFlag.moreFragment = fragmentFlags.substr(2, 1);
        fragmentFlag.fragmentOffset = fragmentFlags.substr(3);

        if (ipv4Header.size() == 23)
        {
            ByteString type = Functions::byteToBin(ipv4Header.substr(20, 1));
            options.type.copy = type.substr(0, 1);
            options.type.classControl = type.substr(1, 2);
            options.type.routerAlert = type.substr(3, 5);
            options.length = ipv4Header.substr(21, 1);
            options.routerAlert = ipv4Header.substr(22, 1);
        }
        //Profiler::getInstance().notify("IPv4 decap end");
        return true;
    }
};

/**
 * @struct IPv6Header
 * @brief Represents an IPv6 header.
 */
struct IPv6Header
{
    ByteString version{};           ///< Version field.
    ByteString trafficClass{};      ///< Traffic Class field.
    ByteString flowLabel{};         ///< Flow Label field.
    ByteString payloadLength{};     ///< Payload Length field.
    ByteString protocol{};          ///< Next Header field.
    ByteString hopLimit{};          ///< Hop Limit field.
    ByteString sourceAddress{};     ///< Source IPv6 address.
    ByteString destinationAddress{};///< Destination IPv6 address.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString ipv6String;
        if (version.size() != 1 || trafficClass.size() != 2 || flowLabel.size() != 5 || payloadLength.size() != 2 ||
            protocol.size() != 1 || hopLimit.size() != 1 || sourceAddress.size() != 16 || 
            destinationAddress.size() != 16) return std::nullopt;

        ipv6String.reserve(40);
        ipv6String += Functions::hexToByte(version + trafficClass + flowLabel);
        ipv6String += payloadLength;
        ipv6String += protocol;
        ipv6String += hopLimit;
        ipv6String += sourceAddress;
        ipv6String += destinationAddress;

        return ipv6String;
    }
    bool decapsulate(const ByteString ipv6Header)
    {
        if (ipv6Header.size() != 40) return false;

        ByteString ipv6Temp = Functions::byteToHex(ipv6Header.substr(0, 4));
        version = ipv6Temp.substr(0, 1);
        trafficClass = ipv6Temp.substr(1, 2);
        flowLabel = ipv6Temp.substr(3, 5);
        payloadLength = ipv6Header.substr(4, 2);
        protocol = ipv6Header.substr(6, 1);
        hopLimit = ipv6Header.substr(7, 1);
        sourceAddress = ipv6Header.substr(8, 16);
        destinationAddress = ipv6Header.substr(24, 16);
        return true;
    }
};

/**
 * @struct TcpHeader
 * @brief Represents a TCP (Transmission Control Protocol) header.
 */
struct TcpHeader
{
    ByteString sourcePort{};        ///< Source port number.
    ByteString destinationPort{};   ///< Destination port number.
    ByteString sequenceNumber{};    ///< Sequence number.
    ByteString ackNumber{};         ///< Acknowledgment number.
    ByteString headerLength{};      ///< Data offset (header length).
    ByteString windowSize{};        ///< Window size.
    ByteString checksum{};          ///< Checksum.
    ByteString urgentPointer{};     ///< Urgent pointer.

    /**
     * @struct Flags
     * @brief Represents TCP flags.
     */
    struct Flags
    {
        ByteString congestionWindowReduced{}; ///< Congestion Window Reduced (CWR) flag.
        ByteString ecnEcho{};                  ///< ECN Echo flag.
        ByteString urgent{};                   ///< Urgent flag.
        ByteString acknowledgement{};          ///< Acknowledgment flag.
        ByteString push{};                     ///< Push flag.
        ByteString reset{};                    ///< Reset flag.
        ByteString syn{};                      ///< SYN flag.
        ByteString fin{};                      ///< FIN flag.
    } flags;

    /**
     * @struct Option
     * @brief Represents a TCP option.
     */
    struct Option
    {
        ByteString type{};   ///< Option type.
        ByteString length{}; ///< Option length.
        ByteString value{};  ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of TCP options

    const std::optional<ByteString> encapsulate() const
    {
        ByteString tcpString;
        if (sourcePort.size() != 2 || destinationPort.size() != 2 || sequenceNumber.size() != 4 || ackNumber.size() != 4 ||
            headerLength.size() != 1 || windowSize.size() != 2 || checksum.size() != 2 || urgentPointer.size() != 2 ||
            flags.congestionWindowReduced.size() != 1 || flags.urgent.size() != 1 || flags.acknowledgement.size() != 1 || flags.ecnEcho.size() != 1 ||
            flags.fin.size() != 1 || flags.push.size() != 1 || flags.reset.size() != 1 || flags.syn.size() != 1) return std::nullopt;
        
        tcpString.reserve(20);
        tcpString += sourcePort;
        tcpString += destinationPort;
        tcpString += sequenceNumber;
        tcpString += ackNumber;
        tcpString += headerLength;
        tcpString += Functions::binToByte(flags.congestionWindowReduced + flags.ecnEcho + flags.urgent + flags.acknowledgement + flags.push + flags.reset + flags.syn + flags.fin);
        tcpString += windowSize;
        tcpString += ByteString(2, 0x00);
        tcpString += urgentPointer;
        // Add TCP options.
        for (auto opt : options)
        {
            tcpString += opt.type;
            tcpString += opt.length;
            tcpString += opt.value;
        }

        return tcpString;
    }
    bool decapsulate(const ByteString tcpHeader)
    {
        if (tcpHeader.size() < 20) return false;

        sourcePort = tcpHeader.substr(0, 2);
        destinationPort = tcpHeader.substr(2, 2);
        sequenceNumber = tcpHeader.substr(4, 4);
        ackNumber = tcpHeader.substr(8, 4);
        headerLength = tcpHeader.substr(12, 1);
        windowSize = tcpHeader.substr(14, 2);
        checksum = tcpHeader.substr(16, 2);
        urgentPointer = tcpHeader.substr(18, 2);

        ByteString flagOpts = Functions::byteToBin(tcpHeader.substr(13, 1));

        flags.congestionWindowReduced = flagOpts.substr(0, 1);
        flags.ecnEcho = flagOpts.substr(1, 1);
        flags.urgent = flagOpts.substr(2, 1);
        flags.acknowledgement = flagOpts.substr(3, 1);
        flags.push = flagOpts.substr(4, 1);
        flags.reset = flagOpts.substr(5, 1);
        flags.syn = flagOpts.substr(6, 1);
        flags.fin = flagOpts.substr(7, 1);

        if (tcpHeader.size() > 20)
        {
            ByteString tcpOptions = tcpHeader.substr(20);
            size_t optionStart = 0;
            while (optionStart != tcpHeader.size() - 20)
            {
                TcpHeader::Option option;
                if (!validateSize(optionStart, 2, tcpOptions)) return false;

                option.type = tcpOptions.substr(optionStart, 1);
                optionStart += 1;
                if (option.type != std::string("\x01", 1))
                {
                    option.length = tcpOptions.substr(optionStart, 1);
                    optionStart += 1;
                    size_t valueLength = static_cast<size_t>(Functions::byteToNum(option.length) - 2);
                    if (!validateSize(optionStart, valueLength, tcpOptions)) return false;
                    option.value = tcpOptions.substr(optionStart, valueLength);
                    optionStart += valueLength;
                }
                options.push_back(option);
            }
        }
        return true;
    }
};

/**
 * @struct UdpHeader
 * @brief Represents a UDP (User Datagram Protocol) header.
 */
struct UdpHeader
{
    ByteString sourcePort{};        ///< Source port number.
    ByteString destinationPort{};   ///< Destination port number.
    ByteString length{};            ///< Length of UDP header and payload.
    ByteString checksum{};          ///< Checksum.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString udpString;
        if (sourcePort.size() != 2 || destinationPort.size() != 2 || length.size() != 2 || checksum.size() != 2) return std::nullopt;

        udpString.reserve(8);
        udpString += sourcePort;
        udpString += destinationPort;
        udpString += length;
        udpString += ByteString(2, 0x00);

        return udpString;
    }
    bool decapsulate(const ByteString udpHeader)
    {
        if (udpHeader.size() != 8) return false;

        sourcePort = udpHeader.substr(0, 2);
        destinationPort = udpHeader.substr(2, 2);
        length = udpHeader.substr(4, 2);
        checksum = udpHeader.substr(6, 2);

        return true;
    }
};

/**
 * @struct IcmpHeader
 * @brief Represents an ICMP (Internet Control Message Protocol) header.
 */
struct IcmpHeader
{
    ByteString type{};             ///< ICMP type.
    ByteString code{};             ///< ICMP code.
    ByteString checksum{};         ///< ICMP checksum.
    ByteString identifier{};       ///< Identifier.
    ByteString sequenceNumber{};   ///< Sequence number.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString icmpString;
        if (type.size() != 1 || code.size() != 1 || checksum.size() != 2 || identifier.size() != 2 || sequenceNumber.size() != 2) return std::nullopt;

        icmpString.reserve(8);
        icmpString += type;
        icmpString += code;
        icmpString += ByteString(2, 0x00);
        icmpString += identifier;
        icmpString += sequenceNumber;

        return icmpString;
    }
    bool decapsulate(const ByteString icmpHeader)
    {
        if (icmpHeader.size() != 8) return false;

        type = icmpHeader.substr(0, 1);
        code = icmpHeader.substr(1, 1);
        checksum = icmpHeader.substr(2, 2);
        identifier = icmpHeader.substr(4, 2);
        sequenceNumber = icmpHeader.substr(6, 2);

        return true;
    }
};

/**
 * @struct IcmpV6Header
 * @brief Represents an ICMPv6 header.
 */
struct IcmpV6Header
{
    ByteString type{};             ///< ICMPv6 type.
    ByteString code{};             ///< ICMPv6 code.
    ByteString checksum{};         ///< ICMPv6 checksum.
    ByteString reserved{};         ///< Reserved field.

    /**
     * @struct Option
     * @brief Represents an ICMPv6 option.
     */
    struct Option
    {
        ByteString option{};   ///< Option type.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of ICMPv6 options.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString icmpv6String;
        if (type.size() != 1 || code.size() != 1 || checksum.size() != 2 || reserved.size() != 4) return std::nullopt;

        icmpv6String.reserve(8);
        icmpv6String += type;
        icmpv6String += code;
        icmpv6String += ByteString(2, 0x00);
        icmpv6String += reserved;
        for (const auto& opt : options)
        {
            icmpv6String += opt.option;
            icmpv6String += opt.length;
            icmpv6String += opt.value;
        }

        return icmpv6String;
    }
    bool decapsulate(const ByteString icmpV6Header)
    {
        size_t icmpv6Start = 0;
        size_t icmpv6End = 0;
        if (icmpV6Header.size() < 8) return false;

        type = icmpV6Header.substr(0, 1);
        code = icmpV6Header.substr(1, 1);
        checksum = icmpV6Header.substr(2, 2);
        reserved = icmpV6Header.substr(4, 4);
        icmpv6Start = 8;
        icmpv6End = icmpV6Header.size();
        
        while (icmpv6Start != icmpv6End)
        {
            IcmpV6Header::Option option;
            if (!validateSize(icmpv6Start, 4, icmpV6Header)) return false;
            option.option = icmpV6Header.substr(icmpv6Start, 2);
            icmpv6Start += 2;
            option.length = icmpV6Header.substr(icmpv6Start, 2);
            icmpv6Start += 2;
            size_t icmpv6ADD = static_cast<size_t>(Functions::byteToNum(option.length) - 4);
            if (!validateSize(icmpv6Start, icmpv6ADD, icmpV6Header)) return false;
            option.value = icmpV6Header.substr(icmpv6Start, icmpv6ADD);
            icmpv6Start += icmpv6ADD;
            options.push_back(option);
        }
        return true;
    }
};

/**
 * @struct IgmpHeader
 * @brief Represents an IGMP (Internet Group Management Protocol) header.
 */
struct IgmpHeader
{
    ByteString type{};             ///< IGMP type.
    ByteString maxRestTime{};      ///< Max Resp Time.
    ByteString checksum{};         ///< IGMP checksum.
    ByteString multicastAddress{}; ///< Multicast address.

    /**
     * @struct V3
     * @brief Represents IGMPv3-specific fields.
     */
    struct V3
    {
        ByteString supress{};        ///< Suppress flag.
        ByteString qrv{};            ///< Querier's Robustness Variable.
        ByteString qqic{};           ///< Querier's Query Interval Code.
        ByteString numSrc{};         ///< Number of Sources.
    } v3;

    const std::optional<ByteString> encapsulate() const
    {
        ByteString igmpString;
        if (type.size() != 1 || maxRestTime.size() != 1 || checksum.size() != 2 || multicastAddress.size() != 4) return std::nullopt;

        igmpString.reserve(8);
        igmpString += type;
        igmpString += maxRestTime;
        igmpString += ByteString(2, 0x00);
        igmpString += multicastAddress;
        if (v3.supress.size() == 1 && v3.qrv.size() == 3 && v3.qqic.size() == 1 && v3.numSrc.size() == 2)
        {
            // NEEDS FURTHER IMPLEMENTATION
            igmpString.reserve(12);
            igmpString += Functions::binToByte(v3.supress + v3.qrv) + v3.qqic + v3.numSrc;
        }

        return igmpString;
    }
    bool decapsulate(const ByteString igmpHeader)
    {
        if (igmpHeader.size() < 8) return false;

        type = igmpHeader.substr(0, 1);
        maxRestTime = igmpHeader.substr(1, 1);
        checksum = igmpHeader.substr(2, 2);
        multicastAddress = igmpHeader.substr(4, 4);

        if (igmpHeader.size() == 12) 
        {
            v3.supress = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(5, 1);
            v3.qrv = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(6, 3);
            v3.qqic = igmpHeader.substr(9, 1);
            v3.numSrc = igmpHeader.substr(10, 2);
        }
        return true;
    }
};

/**
 * @struct TlsHeader
 * @brief Represents a TLS (Transport Layer Security) header.
 */
struct TlsHeader
{
    ByteString type{};        ///< TLS Content Type.
    ByteString version{};     ///< TLS Version.
    ByteString length{};      ///< TLS Length.

    std::optional<ByteString> encapsulate()
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString Header)
    {
        return false;
    }
};

/**
 * @struct GreHeade
 * @brief Represents a GRE (Generic Routing Encapsulation) header.
 */
struct GreHeade
{
    /**
     * @struct Flags
     * @brief Represents GRE flags.
     */
    struct Flags
    {
        ByteString checksum{};              ///< Checksum flag.
        ByteString routing{};                ///< Routing flag.
        ByteString key{};                    ///< Key flag.
        ByteString seqNum{};                 ///< Sequence Number flag.
        ByteString strictSourceRoute{};      ///< Strict Source Route flag.
        ByteString acknowledgment{};         ///< Acknowledgment flag.
        ByteString recursion{};              ///< Recursion flags.
        ByteString reserved{};               ///< Reserved flags.
        ByteString version{};                ///< GRE version.
    } flags;

    ByteString protocol{};        ///< GRE Protocol Type.
    ByteString length{};          ///< GRE Length.
    ByteString callID{};          ///< GRE Call ID.
    ByteString seqNum{};          ///< GRE Sequence Number.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString greString;
        if (flags.checksum.size() != 1 || flags.routing.size() != 1 || flags.key.size() != 1 || flags.seqNum.size() != 1 ||
            flags.strictSourceRoute.size() != 1 || flags.recursion.size() != 3 || flags.acknowledgment.size() != 1 || flags.reserved.size() != 4 ||
            flags.version.size() != 3 || protocol.size() != 2 || length.size() != 2 || callID.size() != 2 || seqNum.size() != 4) return std::nullopt;

        greString.reserve(12);
        greString += Functions::binToByte(flags.checksum + flags.routing + flags.key + flags.seqNum + flags.strictSourceRoute + flags.recursion + flags.acknowledgment + flags.recursion + flags.version);
        greString += protocol;
        greString += length;
        greString += callID;
        greString += seqNum;

        return greString;
    }
    bool decapsulate(const ByteString greHeader)
    {
        if (greHeader.size() != 12) return false;

        ByteString flagOpts = Functions::byteToBin(greHeader.substr(0, 2));
        flags.checksum = flagOpts.substr(0, 1);
        flags.routing = flagOpts.substr(1, 1);
        flags.key = flagOpts.substr(2, 1);
        flags.seqNum = flagOpts.substr(3, 1);
        flags.strictSourceRoute = flagOpts.substr(4, 1);
        flags.recursion = (Functions::byteToBin(greHeader.substr(0, 2))).substr(5, 3);
        flags.acknowledgment = flagOpts.substr(8, 1);
        flags.reserved = (Functions::byteToBin(greHeader.substr(0, 2))).substr(9, 4);
        flags.version = (Functions::byteToBin(greHeader.substr(0, 2))).substr(13, 3);
        protocol = greHeader.substr(2, 2);
        length = greHeader.substr(4, 2);
        callID = greHeader.substr(6, 2);
        seqNum = greHeader.substr(8, 4);

        return true;
    }
};

/**
 * @struct PppHeader
 * @brief Represents a PPP (Point-to-Point Protocol) header.
 */
struct PppHeader
{
    ByteString address{};   ///< PPP Address field.
    ByteString control{};   ///< PPP Control field.
    ByteString protocol{};  ///< PPP Protocol field.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString pppString;
        if (address.size() != 1 || control.size() != 1 || protocol.size() != 2) return std::nullopt;

        pppString.reserve(4);
        pppString += address;
        pppString += control;
        pppString += protocol;

        return pppString;
        
    }
    bool decapsulate(const ByteString pppHeader)
    {
        if (pppHeader.size() != 4) return false;

        address = pppHeader.substr(0, 1);
        control = pppHeader.substr(1, 1);
        protocol = pppHeader.substr(2, 2);

        return true;
    }
};

/**
 * @struct FrameHeader
 * @brief Represents a Frame Relay frame header.
 */
struct FrameHeader
{
    /**
     * @struct FirstAddress
     * @brief Represents the first address field in a Frame Relay frame.
     */
    struct FirstAddress
    {
        ByteString dlci{}; ///< Data Link Connection Identifier (DLCI).
        ByteString cr{};    ///< Control bit.
        ByteString ea{};    ///< Extension Address bit.
    } firstAddress;

    /**
     * @struct SecondAddress
     * @brief Represents the second address field in a Frame Relay frame.
     */
    struct SecondAddress
    {
        ByteString dlci{};   ///< Data Link Connection Identifier (DLCI).
        ByteString fecn{};    ///< Forward Explicit Congestion Notification.
        ByteString becn{};    ///< Backward Explicit Congestion Notification.
        ByteString de{};      ///< Discard Eligibility.
        ByteString ea{};      ///< Extension Address bit.
    } secondAddress;

    ByteString type; ///< Frame Relay Type field.

    std::optional<ByteString> encapsulate()
    {

    }
    bool decapsulate(const ByteString frameHeader)
    {
        if (frameHeader.size() != 4) return false;

        ByteString relay = Functions::byteToBin(frameHeader.substr(0, 1));
        firstAddress.cr = relay.substr(6, 1);
        firstAddress.ea = relay.substr(7, 1);
        relay = Functions::byteToBin(frameHeader.substr(1, 1));
        secondAddress.fecn = relay.substr(4, 1);
        secondAddress.becn = relay.substr(5, 1);
        secondAddress.de = relay.substr(6, 1);
        secondAddress.ea = relay.substr(7, 1);
        type = frameHeader.substr(2, 2);

        return true;
    }
};

/**
 * @struct AhHeader
 * @brief Represents an AH (Authentication Header) header.
 */
struct AhHeader
{
    ByteString next{};      ///< Next Header field.
    ByteString length{};    ///< Payload Length field.
    ByteString reserved{};  ///< Reserved field.
    ByteString spi{};       ///< Security Parameters Index (SPI).
    ByteString sequence{};  ///< Sequence Number.
    ByteString icv{};        ///< Integrity Check Value (ICV).

    const std::optional<ByteString> encapsulate() const
    {
        ByteString ahString;
        if (next.size() != 1 || length.size() != 1 || reserved.size() != 2 || spi.size() != 4 || sequence.size() != 8) return std::nullopt;

        ahString.reserve(12);
        ahString += next;
        ahString += length;
        ahString += reserved;
        ahString += spi;
        ahString += sequence;
        ahString += icv;

        return ahString;
    }
    bool decapsulate(const ByteString ahHeader)
    {
        if (ahHeader.size() < 12)
        {
            next = ahHeader.substr(0, 1);
            length = ahHeader.substr(1, 1);
            reserved = ahHeader.substr(2, 2);
            spi = ahHeader.substr(4, 4);
            sequence = ahHeader.substr(8, 4);
            icv = ahHeader.substr(12);
        }

        return true;
    }
};

/**
 * @struct EspHeader
 * @brief Represents an ESP (Encapsulating Security Payload) header.
 */
struct EspHeader
{
    ByteString spi{};       ///< Security Parameters Index (SPI).
    ByteString sequence{};  ///< Sequence Number.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString espString;
        if (spi.size() != 4 || sequence.size() != 4) return std::nullopt;

        espString.reserve(8);
        espString += spi;
        espString += sequence;

        return espString;
    }
    bool decapsulate(const ByteString espHeader)
    {
        if (espHeader.size() != 8) return false;

        spi = espHeader.substr(0, 4);
        sequence = espHeader.substr(4, 4);

        return true;
    }
};

/**
 * @struct VlanHeader
 * @brief Represents a VLAN (Virtual LAN) header.
 */
struct VlanHeader
{
    ByteString priority{}; ///< VLAN Priority field.
    ByteString dei{};      ///< Drop Eligible Indicator (DEI) field.
    ByteString id{};       ///< VLAN Identifier (VID) field.
    ByteString type{};     ///< EtherType field.

    const std::optional<ByteString> encapsulate() const
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString vlanHeader)
    {
        return false;
    }
};

/**
 * @struct LldpHeader
 * @brief Represents an LLDP (Link Layer Discovery Protocol) header.
 */
struct LldpHeader
{
    /**
     * @struct TLV
     * @brief Represents a Type-Length-Value (TLV) structure in LLDP.
     */
    struct TLV
    {
        ByteString type{};      ///< TLV type.
        ByteString length{};    ///< TLV length.
        ByteString value{};     ///< TLV value.
    };

    TLV chassisID{};               ///< Chassis ID TLV.
    TLV portID{};                   ///< Port ID TLV.
    TLV ttl{};                      ///< Time-To-Live (TTL) TLV.
    TLV portDescription{};          ///< Port Description TLV.
    TLV systemName{};               ///< System Name TLV.
    TLV systemDescription{};        ///< System Description TLV.
    TLV systemCapabilities{};       ///< System Capabilities TLV.
    TLV managementAddress{};        ///< Management Address TLV.
    TLV organizationallySpecific{}; ///< Organizationally Specific TLV.
    TLV endOfLLDPDU{};              ///< End of LLDPDU TLV.

    std::optional<ByteString> encapsulate()
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString Header)
    {
        return false;
    }
};

/**
 * @struct DhcpHeader
 * @brief Represents a DHCP (Dynamic Host Configuration Protocol) header.
 */
struct DhcpHeader
{
    ByteString boot{};                         ///< Boot Message Type.
    ByteString hardwareType{};                 ///< Hardware Type.
    ByteString hardwareAddressLength{};        ///< Hardware Address Length.
    ByteString hops{};                          ///< Hops.
    ByteString transID{};                       ///< Transaction ID.
    ByteString secondsElapsed{};                ///< Seconds Elapsed.
    ByteString clientIP{};                      ///< Client IP Address.
    ByteString yourClientIP{};                  ///< 'Your' (Client) IP Address.
    ByteString nextServerIP{};                  ///< Next Server IP Address.
    ByteString relayAgentIP{};                  ///< Relay Agent IP Address.
    ByteString clientMacAddress{};              ///< Client MAC Address.
    ByteString clientHardwareAddressPadding{};  ///< Padding for Client Hardware Address.
    ByteString serverHostName{};                ///< Server Hostname.
    ByteString bootFile{};                      ///< Boot File Name.
    ByteString magicCookie{};                   ///< Magic Cookie.
    ByteString padding{};                       ///< Padding after DHCP options.
    ByteString end{};                           ///< DHCP Option End Marker.

    /**
     * @struct BootpFlags
     * @brief Represents BOOTP flags.
     */
    struct BootpFlags
    {
        ByteString broadcast{};   ///< Broadcast flag.
        ByteString reserved{};    ///< Reserved bits.
    } bootpFlags;

    /**
     * @struct Option
     * @brief Represents a DHCP option.
     */
    struct Option
    {
        ByteString option{};   ///< Option code.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of DHCP options.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString dhcpString;
        if (boot.size() != 1 || hardwareType.size() != 1 || hardwareAddressLength.size() != 1 || hops.size() != 1 || transID.size() != 4 ||
            secondsElapsed.size() != 2 || bootpFlags.broadcast.size() != 1 || bootpFlags.reserved.size() != 7 || clientIP.size() != 4 ||
            yourClientIP.size() != 4 || nextServerIP.size() != 4 || relayAgentIP.size() != 4 || clientMacAddress.size() != 6 || clientHardwareAddressPadding.size() != 10 ||
            serverHostName.size() != 64 || bootFile.size() != 128 || magicCookie.size() != 4 || end.size() != 1) return std::nullopt;

        dhcpString.reserve(240);
        dhcpString += boot;
        dhcpString += hardwareType;
        dhcpString += hardwareAddressLength;
        dhcpString += hops;
        dhcpString += transID;
        dhcpString += secondsElapsed;
        dhcpString += Functions::binToByte(bootpFlags.broadcast + bootpFlags.reserved);
        dhcpString += clientIP;
        dhcpString += yourClientIP;
        dhcpString += nextServerIP;
        dhcpString += relayAgentIP;
        dhcpString += clientMacAddress;
        dhcpString += clientHardwareAddressPadding;
        dhcpString += serverHostName;
        dhcpString += bootFile;
        dhcpString += magicCookie;
        for (DhcpHeader::Option opt : options)
        {
            dhcpString += opt.option;
            dhcpString += opt.length;
            dhcpString += opt.value;
        }
        dhcpString += end;
        dhcpString += padding;

        return dhcpString;
    }
    bool decapsulate(const ByteString dhcpHeaders)
    {
        size_t dhcpStart;
        ByteString dhcpHeader;
        if (dhcpHeaders.size() < 240) return false;

        dhcpHeader = dhcpHeaders;
        boot = dhcpHeader.substr(0, 1);
        hardwareType = dhcpHeader.substr(1, 1);
        hardwareAddressLength = dhcpHeader.substr(2, 1);
        hops = dhcpHeader.substr(3, 1);
        transID = dhcpHeader.substr(4, 4);
        secondsElapsed = dhcpHeader.substr(8, 2);
        ByteString flags = Functions::byteToBin(dhcpHeader.substr(10, 2));
        bootpFlags.broadcast = flags.substr(0, 1);
        bootpFlags.reserved = flags.substr(1, 7);
        clientIP = dhcpHeader.substr(12, 4);
        yourClientIP = dhcpHeader.substr(16, 4);
        nextServerIP = dhcpHeader.substr(20, 4);
        relayAgentIP = dhcpHeader.substr(24, 4);
        clientMacAddress = dhcpHeader.substr(28, 6);
        clientHardwareAddressPadding = dhcpHeader.substr(34, 10);
        serverHostName = dhcpHeader.substr(44, 64);
        bootFile = dhcpHeader.substr(108, 128);
        magicCookie = dhcpHeader.substr(236, 4);
        dhcpStart = 240;

        size_t dhcpEnd{};
        size_t dhcpLength = dhcpHeader.size();
        for (size_t i = dhcpLength - 1; i >= 0; --i)
        {
            if (static_cast<unsigned char>(dhcpHeader[i]) == 0xff)
            {
                dhcpEnd = i;
                break;
            }
        }

        while (dhcpStart != dhcpEnd)
        {
            DhcpHeader::Option option;
            option.option = dhcpHeader.substr(dhcpStart, 1);
            dhcpStart += 1;
            option.length = dhcpHeader.substr(dhcpStart, 1);
            dhcpStart += 1;
            option.value = dhcpHeader.substr(dhcpStart, static_cast<size_t>(Functions::byteToNum(option.length)));
            size_t dhcpADD = static_cast<size_t>(Functions::byteToNum(option.length));
            dhcpStart += dhcpADD;
            options.push_back(option);
        }

        end = dhcpHeader.substr(dhcpStart, 1);
        padding = dhcpHeader.substr(dhcpStart + 1);
        return true;
    }
};

/**
 * @struct EigrpHeader
 * @brief Represents an EIGRP (Enhanced Interior Gateway Routing Protocol) header.
 */
struct EigrpHeader
{
    ByteString version{};             ///< EIGRP version.
    ByteString opcode{};              ///< EIGRP opcode.
    ByteString checksum{};            ///< EIGRP checksum.
    ByteString sequence{};            ///< Sequence number.
    ByteString ack{};                 ///< Acknowledgment number.
    ByteString virtualRouterID{};     ///< Virtual Router ID.
    ByteString autonomousSystem{};    ///< Autonomous System number.

    /**
     * @struct flags
     * @brief Represents EIGRP flags.
     */
    struct flags
    {
        ByteString init{};                ///< INIT flag.
        ByteString conditionalRecieve{};  ///< Conditional Receive flag.
        ByteString restart{};             ///< Restart flag.
        ByteString endOfTable{};          ///< End Of Table flag.
    } flags;

    /**
     * @struct Option
     * @brief Represents an EIGRP option.
     */
    struct Option
    {
        ByteString option{};   ///< Option code.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of EIGRP options.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString eigrpString;
        if (version.size() != 1 || opcode.size() != 1 || checksum.size() != 2 || flags.endOfTable.size() != 1 || flags.conditionalRecieve.size() != 1 || flags.init.size() != 1 || flags.restart.size() != 1 ||
            sequence.size() != 4 || ack.size() != 4 || virtualRouterID.size() != 2 || autonomousSystem.size() != 2) return std::nullopt;

        eigrpString.reserve(20);
        eigrpString += version;
        eigrpString += opcode;
        eigrpString += ByteString(2, 0x00);
        eigrpString += Functions::binToByte(ByteString("0000000000000000000000000000") + flags.endOfTable + flags.restart + flags.conditionalRecieve + flags.init);
        eigrpString += sequence;
        eigrpString += ack;
        eigrpString += virtualRouterID;
        eigrpString += autonomousSystem;
        for (auto opt : options)
        {
            eigrpString += opt.option;
            eigrpString += opt.length;
            eigrpString += opt.value;
        }
            
        return eigrpString;
    }
    bool decapsulate(const ByteString eigrpHeader)
    {
        size_t eigrpStart;
        size_t eigrpEnd;
        if (eigrpHeader.size() < 20) return false;

        version = eigrpHeader.substr(0, 1);
        opcode = eigrpHeader.substr(1, 1);
        checksum = eigrpHeader.substr(2, 2);
        ByteString flag = Functions::byteToBin(eigrpHeader.substr(4, 4));
        flags.endOfTable = flag.substr(28, 1);
        flags.restart = flag.substr(29, 1);
        flags.conditionalRecieve = flag.substr(30, 1);
        flags.init = flag.substr(31, 1);
        sequence = eigrpHeader.substr(8, 4);
        ack = eigrpHeader.substr(12, 4);
        virtualRouterID = eigrpHeader.substr(16, 2);
        autonomousSystem = eigrpHeader.substr(18, 2);
        eigrpStart = 20;
        eigrpEnd = eigrpHeader.size();

        while (eigrpStart != eigrpEnd)
        {
            EigrpHeader::Option option;
            if (!validateSize(eigrpStart, 4, eigrpHeader)) return false;
            option.option = eigrpHeader.substr(eigrpStart, 2);
            eigrpStart += 2;
            option.length = eigrpHeader.substr(eigrpStart, 2);
            eigrpStart += 2;
            size_t eigrpADD = static_cast<size_t>(Functions::byteToNum(option.length) - 4);
            if (validateSize(eigrpStart, eigrpADD, eigrpHeader))
            {
                option.value = eigrpHeader.substr(eigrpStart, eigrpADD);
                eigrpStart += eigrpADD;
                options.push_back(option);
            }
        }
        return true;
    }
};

/**
 * @struct OspfPacket::ospfHeader
 * @brief Represents an OSPF (Open Shortest Path First) packet header.
 */
namespace OspfPacket
{
    struct ospfHeader
    {
        ByteString version{};       ///< OSPF version.
        ByteString type{};          ///< OSPF packet type.
        ByteString packetLength{};  ///< OSPF packet length.
        ByteString sourceRouter{};  ///< OSPF source router ID.
        ByteString areaID{};         ///< OSPF Area ID.
        ByteString checksum{};       ///< OSPF checksum.
        ByteString authType{};       ///< OSPF authentication type.
        ByteString authData{};       ///< OSPF authentication data.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfHelloHeader
    {
        ByteString mask{};             ///< OSPF Hello mask.
        ByteString helloInterval{};    ///< OSPF Hello interval.
        ByteString routerPriority{};   ///< OSPF Router Priority.
        ByteString routerDeadInterval{}; ///< OSPF Router Dead Interval.
        ByteString designatedRouter{}; ///< OSPF Designated Router.
        ByteString backupDesignatedRouter{}; ///< OSPF Backup Designated Router.
        ByteString activeNeighbor{};    ///< OSPF Active Neighbor.

        /**
         * @struct options
         * @brief Represents OSPF Hello options.
         */
        struct options
        {
            ByteString notSet{};           ///< Not Set option.
            ByteString opaque{};            ///< Opaque option.
            ByteString demand{};            ///< Demand option.
            ByteString llsPresent{};        ///< Link-Layer Signaling Present option.
            ByteString nssa{};              ///< NSSA option.
            ByteString multicast{};         ///< Multicast option.
            ByteString externalRouting{};   ///< External Routing option.
            ByteString multiTopology{};     ///< Multi-Topology Routing option.
        } options;

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfDescriptionHeader
    {
        ByteString interfaceMtu{};      ///< OSPF Interface MTU.
        ByteString sequence{};          ///< OSPF Sequence number.

        /**
         * @struct options
         * @brief Represents OSPF Description options.
         */
        struct options
        {
            ByteString notSet{};           ///< Not Set option.
            ByteString opaque{};            ///< Opaque option.
            ByteString demand{};            ///< Demand option.
            ByteString llsPresent{};        ///< Link-Layer Signaling Present option.
            ByteString nssa{};              ///< NSSA option.
            ByteString multicast{};         ///< Multicast option.
            ByteString externalRouting{};   ///< External Routing option.
            ByteString multiTopology{};     ///< Multi-Topology Routing option.
        } options;

        /**
         * @struct description
         * @brief Represents OSPF Description flags.
         */
        struct description
        {
            ByteString OOBResync{};      ///< Out-of-Band Resynchronization flag.
            ByteString init{};            ///< INIT flag.
            ByteString more{};            ///< MORE flag.
            ByteString master{};          ///< MASTER flag.
        } description;

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfRequest
    {
        ByteString lsType{};           ///< Link State Type.
        ByteString linkStatID{};       ///< Link State ID.
        ByteString advertisingRouter{}; ///< Advertising Router.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfLLSHeader
    {
        ByteString checksum{};     ///< OSPF Link-Layer Signaling checksum.
        ByteString dataLength{};   ///< OSPF Link-Layer Signaling data length.
        ByteString options{};      ///< OSPF Link-Layer Signaling options.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct LSA
    {
        ByteString lsAge{};         ///< Link State Advertisement age.
        ByteString doNotAge{};      ///< Do Not Age flag.
        ByteString lsType{};        ///< Link State type.
        ByteString linkStateID{};   ///< Link State ID.
        ByteString advertisingRouter{}; ///< Advertising Router ID.
        ByteString sequenceNumber{}; ///< Sequence Number.
        ByteString checksum{};       ///< Link State Advertisement checksum.
        ByteString length{};         ///< Link State Advertisement length.

        /**
         * @struct options
         * @brief Represents LSA options.
         */
        struct options
        {
            ByteString notSet{};           ///< Not Set option.
            ByteString opaque{};            ///< Opaque option.
            ByteString demand{};            ///< Demand option.
            ByteString llsPresent{};        ///< Link-Layer Signaling Present option.
            ByteString nssa{};              ///< NSSA option.
            ByteString multicast{};         ///< Multicast option.
            ByteString externalRouting{};   ///< External Routing option.
            ByteString multiTopology{};     ///< Multi-Topology Routing option.
        } options;

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };

    struct ospfUpdateheader
    {
        ByteString numOfLsa{}; ///< Number of LSAs in the Update.
        std::vector<LSA> lsa{}; ///< Vector of LSAs.

        std::optional<ByteString> encapsulate()
        {
            return std::nullopt;
        }
        bool decapsulate(const ByteString Header)
        {
            return false;
        }
    };
}

/**
 * @struct SyslogHeader
 * @brief Represents a Syslog packet header.
 */
struct SyslogHeader {
    ByteString PRI{};      ///< Priority value.
    ByteString message{};  ///< Syslog message content.

    std::optional<ByteString> encapsulate()
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString syslogHeader)
    {
        return false;
    }
};

// Layer 2 Variants
using Layer2Variant = std::variant<EthernetHeader, PppHeader, FrameHeader>;

// Layer 2.5 Variants
using Layer2_5Variant = std::variant<ArpHeader, MplsHeader, VlanHeader, LldpHeader>;

// Layer 3 Variants
using Layer3Variant = std::variant<IPv4Header, IPv6Header, GreHeade, AhHeader, EspHeader, IcmpHeader, IcmpV6Header, IgmpHeader>;

// Layer 4 Variants
using Layer4Variant = std::variant<TcpHeader, UdpHeader, EigrpHeader>;

// Layer 5 Variants
using Layer5Variant = std::variant<DhcpHeader>;

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
    std::vector<Layer5Variant> Layer5{};    ///< Vector of Layer 5 headers (e.g., DHCP).
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

#endif // PACKET_STRUCTURE_H
