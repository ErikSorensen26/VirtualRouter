#pragma once

#include <any>
#include <typeinfo>
#include <Functions.h>
#include <ByteString.hpp>

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
        inline const std::string ipv6("\x86\xdd", 3);   ///< EtherType for IPv6
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
        inline const std::string esp("\x32", 1);      ///< IP protocol number for ESP.
        inline const std::string ah("\x33", 1);       ///< IP protocol number for AH.
        inline const std::string gre("\x2f", 1);      ///< IP protocol number for GRE.
        inline const std::string igmp("\x02", 1);     ///< IP protocol number for IGMP.
        inline const std::string none("\x3b", 1);     ///< IP protocol number for None.
        inline const std::string tcp("\x06", 1);      ///< IP protocol number for TCP.
        inline const std::string udp("\x11", 1);      ///< IP protocol number for UDP.
        inline const std::string icmp("\x3a", 1);     ///< IP protocol number for ICMP.
        inline const std::string sctp("\x84", 1);     ///< IP protocol number for SCTP.
        inline const std::string eigrp("\x58", 1);    ///< IP protocol number for EIGRP.
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
        inline const std::string end("\xff", 1);                           ///< DHCP Option End Marker.
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

/**
 * @struct EthernetHeader
 * @brief Represents an Ethernet frame header.
 */
struct EthernetHeader
{
    ByteString
        sourceMac{},
        destinationMac{},
        type{};
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
};

/**
 * @struct EspHeader
 * @brief Represents an ESP (Encapsulating Security Payload) header.
 */
struct EspHeader
{
    ByteString spi{};       ///< Security Parameters Index (SPI).
    ByteString sequence{};  ///< Sequence Number.
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
        uint8_t type;        ///< TLV type.
        uint16_t length;     ///< TLV length.
        ByteString value{};  ///< TLV value.
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
    };

    struct ospfRequest
    {
        ByteString lsType{};           ///< Link State Type.
        ByteString linkStatID{};       ///< Link State ID.
        ByteString advertisingRouter{}; ///< Advertising Router.
    };

    struct ospfLLSHeader
    {
        ByteString checksum{};     ///< OSPF Link-Layer Signaling checksum.
        ByteString dataLength{};   ///< OSPF Link-Layer Signaling data length.
        ByteString options{};      ///< OSPF Link-Layer Signaling options.
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
    };

    struct ospfUpdateheader
    {
        ByteString numOfLsa{}; ///< Number of LSAs in the Update.
        std::vector<LSA> lsa{}; ///< Vector of LSAs.
    };
}

/**
 * @struct SyslogHeader
 * @brief Represents a Syslog packet header.
 */
struct SyslogHeader {
    ByteString PRI{};      ///< Priority value.
    ByteString message{};  ///< Syslog message content.
};

/**
 * @struct PacketInfo
 * @brief Aggregates packet information across different protocol layers.
 */
struct PacketInfo
{
    std::vector<std::any> Layer2{};    ///< Vector of Layer 2 headers (e.g., Ethernet, PPP).
    std::vector<std::any> Layer2_5{};  ///< Vector of Layer 2.5 headers (e.g., ARP, MPLS, VLAN, LLDP).
    std::vector<std::any> Layer3{};    ///< Vector of Layer 3 headers (e.g., IPv4, IPv6, GRE, AH, ESP, ICMP, IGMP, EIGRP).
    std::vector<std::any> Layer4{};    ///< Vector of Layer 4 headers (e.g., TCP, UDP).
    std::vector<std::any> Layer5{};    ///< Vector of Layer 5 headers (e.g., DHCP).
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
    return a.type() == typeid(T);
}
