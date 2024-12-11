#pragma once

#include <any>
#include <typeinfo>
#include <Functions.h>
#include <ByteString.hpp>

namespace Variable
{
    namespace Ethernet
    {
        inline const std::string arp("\x08\x06", 2);
        inline const std::string ipv4("\x08\x00", 2);
        inline const std::string ipv6("\x86\xdd", 3);
        inline const std::string mpls("\x88\x47", 2);
        inline const std::string vlan("\x86\xdd", 2);
        inline const std::string lldp("\x88\xcc", 2);
    }
    namespace Arp
    {
        inline const std::string ethernet("\x00\x01", 2);
        inline const std::string ipv4("\x08\x00", 2);
        namespace Opcode
        {
            inline const std::string request("\x00\x01", 2);
            inline const std::string reply("\x00\x02", 2);
            inline const std::string reverseRequest("\x00\x03", 2);
            inline const std::string reverseReply("\x00\x04", 2);
            inline const std::string dynamicRequest("\x00\x05", 2);
            inline const std::string dynamicReply("\x00\x06", 2);
            inline const std::string dynamicError("\x00\x07", 2);
            inline const std::string inverseRequest("\x00\x08", 2);
            inline const std::string inverseReply("\x00\x09", 2);
            inline const std::string nak("\x00\x0a", 2);
        }
    }
    namespace IP
    {
        inline const std::string esp("\x32", 1);
        inline const std::string ah("\x33", 1);
        inline const std::string gre("\x2f", 1);
        inline const std::string igmp("\x02", 1);
        inline const std::string none("\x3b", 1);
        inline const std::string tcp("\x06", 1);
        inline const std::string udp("\x11", 1);
        inline const std::string icmp("\x3a", 1);
        inline const std::string sctp("\x84", 1);
        inline const std::string eigrp("\x58", 1);
    }
    namespace Udp
    {
        namespace Dhcp
        {
            inline const std::string source("\x00\x44", 2);
            inline const std::string destination("\x00\x43", 2);
        }
    }
    namespace Gre
    {
        inline const std::string ppp("\x88\x0b", 2);
    }
    namespace Ah
    {
        inline const std::string esp("\x32", 1);
    }
    namespace Mac
    {
        inline const std::string broadcast(6, '\xff');
        inline const std::string source(6, '\x00');
    }
    namespace IPv4
    {
        inline const std::string broadcast(4, '\xff');
        inline const std::string source(4, '\x00');
    }
    namespace Dhcp
    {
        namespace Type
        {
            inline const std::string discover("\x01", 1);
            inline const std::string offer("\x02", 1);
            inline const std::string request("\x03", 1);
            inline const std::string ack("\x05", 1);
            inline const std::string nac("\x06", 1);
        }
        namespace Option
        {
            inline const std::string mask("\x01", 1);
            inline const std::string broadcast("\x1c", 1);
            inline const std::string timeOffset("\x02", 1);
            inline const std::string router("\x03", 1);
            inline const std::string domainName("\x0f", 1);
            inline const std::string domainServer("\x06", 1);
            inline const std::string domainSearch("\x77", 1);
            inline const std::string netbiosNameServer("\x2c", 1);
            inline const std::string netbiosScope("\x2c", 1);
            inline const std::string mtu("\x1a", 1);
            inline const std::string classlessStateRoute("\x79", 1);
            inline const std::string ntp("\x2a", 1);
            inline const std::string type("\x35", 1);
            inline const std::string hostname("\x0c", 1);
            inline const std::string clientID("\x3d", 1);
            inline const std::string serverIdentifier("\x36", 1);
            inline const std::string leaseTime("\x33", 1);
            inline const std::string renewalTime("\x3a", 1);
            inline const std::string rebindingTime("\x3b", 1);
            inline const std::string requestIP("\x32", 1);
            inline const std::string requestList("\x37", 1);
            inline const std::string maxSize("\x39", 1);
        }
        inline const std::string clientHardwareAddressPadding(10, '\x00');
        inline const std::string serverHostName(64, '\x00');
        inline const std::string bootfile(128, '\x00');
        inline const std::string endPadding(25, '\x00');
        inline const std::string end("\xff", 1);
        inline const std::string magicCookie("\x63\x82\x53\x63", 4);
    }
    namespace Eigrp
    {
        namespace Type
        {
            inline const std::string update("\x01", 1);
            inline const std::string request("\x02", 1);
            inline const std::string query("\x03", 1);
            inline const std::string reply("\x04", 1);
            inline const std::string hello("\x05", 1);
        }
        namespace Option
        {
            inline const std::string parameter("\x00\x01", 2);
            inline const std::string version("\x00\x04", 2);
            inline const std::string sequence("\x00\x03", 2);
            inline const std::string multicastSequence("\x00\x05", 2);
            inline const std::string internalRoute("\x01\x02", 2);
            inline const std::string externalRoute("\x01\x03", 2);
            inline const std::string internalRouteV6("\x04\02");
            inline const std::string externalRouteV6("\x04\x03");
            inline const std::string stub("\x00\x06", 2);
            inline const std::string authentication("\x00\x02");
        }
        namespace Version
        {
            inline const std::string release("\x0c\x04", 2);
            inline const std::string tls("\x01\x02", 2);
        }
        namespace ExternalProtocol 
        {
            inline const std::string igrp("\x01", 1);
            inline const std::string eigrp("\x02", 1);
            inline const std::string staticRoute("\x03", 1);
            inline const std::string rip("\x04", 1);
            inline const std::string hello("\x05", 1);
            inline const std::string ospf("\x06", 1);
            inline const std::string isis("\x07", 1);
            inline const std::string egp("\x08", 1);
            inline const std::string bgp("\x09", 1);
            inline const std::string idrp("\x0a", 1);
            inline const std::string connected("\x0b", 1);
        }
        namespace DestinationAssignmentEncoding {
            inline const std::string ipv4("\x01", 1);
            inline const std::string ipv6("\x02", 1);
            inline const std::string commonService("\x40\x00", 2);
            inline const std::string ipv4Family("\x40\x01", 2);
            inline const std::string ipv6Famil("\x40\x02", 2); 
        }
        namespace CommunityAttribute {
            inline const std::string EXTCOMM_EIGRP("\x00", 1);
            inline const std::string EXTCOMM_DAD("\x01", 1);
            inline const std::string EXTCOMM_VRHB("\x02", 1);
            inline const std::string EXTCOMM_SRLM("\x03", 1);
            inline const std::string EXTCOMM_SAR("\x04", 1);
            inline const std::string EXTCOMM_RPM("\x05", 1);
            inline const std::string EXTCOMM_VRR("\x06", 1);
        }
    }
    namespace Multicast
    {
        namespace Eigrp
        {
            inline const std::string address("\xe0\x00\x00\x0a", 4);
            inline const std::string addressv6("\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a");
            inline const std::string mac("\x01\x00\x5e\x00\x00\x0a", 6);
            inline const std::string macv6("\x33\x33\x00\x00\x00\x0a");
        }
    }
}

// Ethernet header
struct EthernetHeader
{
    ByteString
        sourceMac{},
        destinationMac{},
        type{};
};
// Arp header
struct ArpHeader
{
    ByteString
        hardwareType{},
        protocolType{},
        hardwareSize{},
        protocolSize{},
        opcode{},
        senderHardwareAddress{},
        senderIpAddress{},
        targetHardwareAddress{},
        targetIpAddress{};
};
// Mpls header
struct MplsHeader
{
    ByteString bottomLabelStack{};
    ByteString
        label{},
        expBit{},
        TTL{};
};
// Ipv4 header
struct IPv4Header
{
    ByteString
        version{},
        headerLength{},
        serviceField{},
        totalLength{},
        identification{},
        TTL{},
        protocol{},
        checksum{},
        sourceAddress{},
        destinationAddress{};

    struct FragmentFlag
    {
        ByteString reserved{},
            fragment{},
            moreFragment{};
        ByteString fragmentOffset{};
    } fragmentFlag;

    struct Options
    {
        struct Type
        {
            ByteString copy{};
            ByteString
                classControl{},
                routerAlert{};
        } type;
        ByteString
            length{},
            routerAlert{};
    } options;
};
// Ipv6 header
struct IPv6Header
{
    ByteString
        version{},
        trafficClass{},
        flowLabel{},
        payloadLength{},
        protocol{},
        hopLimit{},
        sourceAddress{},
        destinationAddress{};
};

// Tcp header
struct TcpHeader
{
    ByteString
        sourcePort{},
        destinationPort{},
        sequenceNumber{},
        ackNumber{},
        headerLength{},
        windowSize{},
        checksum{},
        urgentPointer{};

    struct Flags
    {
        ByteString congestionWindowReduced{},
            ecnEcho{}, urgent{}, acknowledgement{},
            push{}, reset{}, syn{}, fin{};
    } flags;

    struct Option
    {
        ByteString type{}, length{}, value{};
    };

    std::vector<Option> options{};
};
// Udp header
struct UdpHeader
{
    ByteString
        sourcePort{},
        destinationPort{},
        length{},
        checksum{};
};
// Icmp header
struct IcmpHeader
{
    ByteString
        type{},
        code{},
        checksum{},
        identifier{},
        sequenceNumber{};
};
// Icmpv6 header
struct IcmpV6Header
{
    ByteString
        type{},
        code{},
        checksum{},
        reserved{};

    struct Option
    {
        ByteString option{}, length{}, value{};
    };

    std::vector<Option> options{};
};
// Igmp header
struct IgmpHeader
{
    ByteString
        type{},
        maxRestTime{},
        checksum{},
        multicastAddress{};

    struct V3
    {
        ByteString supress{};
        ByteString
            qrv{},
            qqic{},
            numSrc{};
    } v3;
};
// Tls header
struct TlsHeader
{
    ByteString
        type{},
        version{},
        length{};
};
// Gre header
struct GreHeade
{
    struct Flags
    {
        ByteString
            checksum{},
            routing{},
            key{},
            seqNum{},
            strictSourceRoute{},
            acknowledgment{};
        ByteString
            recursion{},
            reserved{},
            version{};
    } flags;
    ByteString
        protocol{},
        length{},
        callID{},
        seqNum{};
};
struct PppHeader
{
    ByteString
        address{},
        control{},
        protocol{};
};
struct FrameHeader
{
    struct FirstAddress
    {
        ByteString dlci{}, cr{}, ea{};
    } firstAddress;
    struct SecondAddress
    {
        ByteString dlci{}, fecn{}, becn{}, de{}, ea{};
    } secondAddress;
    ByteString type;
};
// Ah header
struct AhHeader
{
    ByteString
        next{},
        length{},
        reserved{},
        spi{},
        sequence{},
        icv{};
};
// Esp header
struct EspHeader
{
    ByteString spi{}, sequence{};
};
struct VlanHeader
{
    ByteString
        priority{},
        dei{},
        id{},
        type{};
};
// Lldp header
struct LldpHeader
{
    struct TLV
    {
        uint8_t type;
        uint16_t length;
        ByteString value{};
    };

    TLV chassisID;
    TLV portID;
    TLV ttl;
    TLV portDescription;
    TLV systemName;
    TLV systemDescription;
    TLV systemCapabilities;
    TLV managementAddress;
    TLV organizationallySpecific;
    TLV endOfLLDPDU;
};
// Dhcp header
struct DhcpHeader
{
    ByteString
        boot{},
        hardwareType{},
        hardwareAddressLength{},
        hops{},
        transID{},
        secondsElapsed{},
        clientIP{},
        yourClientIP{},
        nextServerIP{},
        relayAgentIP{},
        clientMacAddress{},
        clientHardwareAddressPadding{},
        serverHostName{},
        bootFile{},
        magicCookie{},
        padding{},
        end{};

    struct BootpFlags
    {
        ByteString broadcast{};
        ByteString reserved{};
    } bootpFlags;

    struct Option
    {
        ByteString option{}, length{}, value{};
    };

    std::vector<Option> options{};
};
// Eigrp header
struct EigrpHeader
{
    ByteString version{},
        opcode{},
        checksum{},
        sequence{},
        ack{},
        virtualRouterID{},
        autonomousSystem{};

    struct flags
    {
        ByteString init{},
            conditionalRecieve{},
            restart{},
            endOfTable{};
    } flags;

    struct Option
    {
        ByteString option{}, length{}, value{};
    };
    std::vector<Option> options{};
};
// Ospf header
namespace OspfPacket
{
    struct ospfHeader
    {
        ByteString version{},
            type{},
            packetLength{},
            sourceRouter{},
            areaID{},
            checksum{},
            authType{},
            authData{};
    };

    struct ospfHelloHeader
    {
        ByteString mask{},
            helloInterval{},
            routerPriority{},
            routerDeadInterval{},
            designatedRouter{},
            backupDesignatedRouter{},
            activeNeighbor{};

        struct options
        {
            ByteString notSet{},
                opaque{},
                demand{},
                llsPresent{},
                nssa{},
                multicast{},
                externalRouting{},
                multiTopology{};
        } options;
    };

    struct ospfDescriptionHeader
    {
        ByteString interfaceMtu{},
            sequence{};

        struct options
        {
            ByteString notSet{},
                opaque{},
                demand{},
                llsPresent{},
                nssa{},
                multicast{},
                externalRouting{},
                multiTopology{};
        } options;

        struct description
        {
            ByteString OOBResync{},
                init{},
                more{},
                master{};
        } description;
    };

    struct ospfRequest
    {
        ByteString lsType{},
            linkStatID{},
            advertisingRouter{};
    };

    struct ospfLLSHeader
    {
        ByteString checksum{},
            dataLength{},
            options{};
    };

    struct LSA
    {
        ByteString lsAge{},
            doNotAge{},
            lsType{},
            linkStateID{},
            advertisingRouter{},
            sequenceNumber{},
            checksum{},
            length{};

        struct options
        {
            ByteString notSet{},
                opaque{},
                demand{},
                llsPresent{},
                nssa{},
                multicast{},
                externalRouting{},
                multiTopology{};
        } options;
    };

    struct ospfUpdateheader
    {
        ByteString numOfLsa{};
        std::vector<OspfPacket::LSA> lsa{};
    };
}
// Syslog Packet
struct SyslogHeader {
    ByteString PRI{},
        message{};
};

// Packet structure
struct PacketInfo
{
    std::vector<std::any> Layer2;
    std::vector<std::any> Layer2_5;
    std::vector<std::any> Layer3;
    std::vector<std::any> Layer4;
    std::vector<std::any> Layer5;
};
// Type test
template <typename T>
bool is_type(const std::any &a)
{
    return a.type() == typeid(T);
}
