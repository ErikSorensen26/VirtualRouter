#pragma once

#include <string>
#include <vector>
#include <any>
#include <typeinfo>
#include <Functions.h>

using namespace std;

struct Variable
{
    struct ethernet
    {
        string arp = Functions::hexToByte("0806");
        string ipv4 = Functions::hexToByte("0800");
        string mpls = Functions::hexToByte("8847");
        string vlan = Functions::hexToByte("86dd");
        string lldp = Functions::hexToByte("88cc");
    } ethernet;
    struct arp
    {
        string ethernet = Functions::hexToByte("0001");
        string ipv4 = Functions::hexToByte("0800");
        struct opcode
        {
            string request = Functions::hexToByte("0001");
            string reply = Functions::hexToByte("0002");
            string reverseRequest = Functions::hexToByte("0003");
            string reverseReply = Functions::hexToByte("0004");
            string dynamicRequest = Functions::hexToByte("0005");
            string dynamicReply = Functions::hexToByte("0006");
            string dynamicError = Functions::hexToByte("0007");
            string inverseRequest = Functions::hexToByte("0008");
            string inverseReply = Functions::hexToByte("0009");
            string nak = Functions::hexToByte("000a");
        } opcode;
    } arp;
    struct ipv4
    {
        std::string GetValue() const { return "4"; }
        string gre = Functions::hexToByte("2f");
        string tcp = Functions::hexToByte("06");
        string udp = Functions::hexToByte("11");
        string icmp = Functions::hexToByte("01");
        string igmp = Functions::hexToByte("02");
        string ah = Functions::hexToByte("33");
        string eigrp = Functions::hexToByte("58");
    } ipv4;
    struct udp
    {
        struct dhcp
        {
            string source = Functions::hexToByte("0044");
            string destination = Functions::hexToByte("0043");
        } dhcp;
    } udp;
    struct gre
    {
        string ppp = Functions::hexToByte("880b");
    } gre;
    struct ah
    {
        string esp = Functions::hexToByte("32");
    } ah;
    struct mac
    {
        string broadcast = Functions::hexToByte("ffffffffffff");
        string source = Functions::hexToByte("000000000000");
    } mac;
    struct ip
    {
        string broadcast = Functions::hexToByte("ffffffff");
        string source = Functions::hexToByte("00000000");
    } ip;
    struct dhcp
    {
        struct type
        {
            string discover = Functions::hexToByte("01");
            string offer = Functions::hexToByte("02");
            string request = Functions::hexToByte("03");
            string ack = Functions::hexToByte("05");
            string nac = Functions::hexToByte("06");
        } type;
        struct options
        {
            string mask = Functions::hexToByte("01");
            string broadcast = Functions::hexToByte("1c");
            string timeOffset = Functions::hexToByte("02");
            string router = Functions::hexToByte("03");
            string domainName = Functions::hexToByte("0f");
            string domainServer = Functions::hexToByte("06");
            string domainSearch = Functions::hexToByte("77");
            string netbiosNameServer = Functions::hexToByte("2c");
            string netbiosScope = Functions::hexToByte("2c");
            string mtu = Functions::hexToByte("1a");
            string classlessStateRoute = Functions::hexToByte("79");
            string ntp = Functions::hexToByte("2a");
            string type = Functions::hexToByte("35");
            string hostname = Functions::hexToByte("0c");
            string clientID = Functions::hexToByte("3d");
            string serverIdentifier = Functions::hexToByte("36");
            string leaseTime = Functions::hexToByte("33");
            string renewalTime = Functions::hexToByte("3a");
            string rebindingTime = Functions::hexToByte("3b");
            string requestIP = Functions::hexToByte("32");
            string requestList = Functions::hexToByte("37");
            string maxSize = Functions::hexToByte("39");
        } options;
        string clientHardwareAddressPadding = Functions::hexToByte("00000000000000000000");
        string serverHostName = Functions::hexToByte("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
        string bootfile = Functions::hexToByte("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
        string endPadding = Functions::hexToByte("00000000000000000000000000000000000000000000000000");
        string end = Functions::hexToByte("ff");
        string magicCookie = Functions::hexToByte("63825363");
    } dhcp;
    struct eigrp
    {
        struct type
        {
            string update = Functions::hexToByte("01");
            string request = Functions::hexToByte("02");
            string query = Functions::hexToByte("03");
            string reply = Functions::hexToByte("04");
            string hello = Functions::hexToByte("05");
        } type;
        struct options
        {
            string parameter = Functions::hexToByte("0001");
            string version = Functions::hexToByte("0004");
            string sequence = Functions::hexToByte("0003");
            string multicastSequence = Functions::hexToByte("0005");
            string internalRoute = Functions::hexToByte("0102");
            string externalRoute = Functions::hexToByte("0202");
        } options;
        struct version
        {
            string release = Functions::hexToByte("0c04");
            string tls = Functions::hexToByte("0102");
        } version;
        struct externalProtocol {
            string igrp = Functions::hexToByte("01");
            string eigrp = Functions::hexToByte("02");
            string staticRoute = Functions::hexToByte("03");
            string rip = Functions::hexToByte("04");
            string hello = Functions::hexToByte("05");
            string ospf = Functions::hexToByte("06");
            string isis = Functions::hexToByte("07");
            string egp = Functions::hexToByte("08");
            string bgp = Functions::hexToByte("09");
            string idrp = Functions::hexToByte("0a");
            string connected = Functions::hexToByte("0b");
        } externalProtocol;
        struct destinationAssignmentEncoding {
            string ipv4 = Functions::hexToByte("01");
            string ipv6 = Functions::hexToByte("02");
            string commonService = Functions::hexToByte("4000");
            string ipv4Family = Functions::hexToByte("4001");
            string ipv6Family= Functions::hexToByte("4002"); 
        } destinationAssignmentEncoding;
        struct communityAttribute {
            string EXTCOMM_EIGRP = Functions::hexToByte("00");
            string EXTCOMM_DAD = Functions::hexToByte("01");
            string EXTCOMM_VRHB = Functions::hexToByte("02");
            string EXTCOMM_SRLM = Functions::hexToByte("03");
            string EXTCOMM_SAR = Functions::hexToByte("04");
            string EXTCOMM_RPM = Functions::hexToByte("05");
            string EXTCOMM_VRR = Functions::hexToByte("06");
        } communityAttribute;
    } eigrp;
    struct multicast
    {
        struct Eigrp
        {
            string address = Functions::hexToByte("e000000a");
            string mac = Functions::hexToByte("01005e00000a");
        } eigrp;
    } multicast;
};

// Ethernet header
struct ethernetHeader
{
    string
        sourceMac{},
        destinationMac{},
        type{};
};
// Arp header
struct arpHeader
{
    string
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
struct mplsHeader
{
    string bottomLabelStack{};
    string
        label{},
        expBit{},
        TTL{};
};
// Ipv4 header
struct ipv4Header
{
    string
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

    struct fragmentFlag
    {
        string reserved{},
            fragment{},
            moreFragment{};
        string fragmentOffset{};
    } fragmentFlag;

    struct options
    {
        struct type
        {
            string copy{};
            string
                classControl{},
                routerAlert{};
        } type;
        string
            length{},
            routerAlert{};
    } options;
};
// Tcp header
struct tcpHeader
{
    string
        sourcePort{},
        destinationPort{},
        sequenceNumber{},
        ackNumber{},
        headerLength{},
        windowSize{},
        checksum{},
        urgentPointer{};

    struct flags
    {
        string congestionWindowReduced{},
            ecnEcho{}, urgent{}, acknowledgement{},
            push{}, reset{}, syn{}, fin{};
    } flags;

    struct Option
    {
        string type{}, length{}, value{};
    };

    vector<Option> options{};
};
// Udp header
struct udpHeader
{
    string
        sourcePort{},
        destinationPort{},
        length{},
        checksum{};
};
// Icmp header
struct icmpHeader
{
    string
        type{},
        code{},
        checksum{},
        identifier{},
        sequenceNumber{};
};
// Igmp header
struct igmpHeader
{
    string
        type{},
        maxRestTime{},
        checksum{},
        multicastAddress{};

    struct v3
    {
        string supress{};
        string
            qrv{},
            qqic{},
            numSrc{};
    } v3;
};
// Tls header
struct tlsHeader
{
    string
        type{},
        version{},
        length{};
};
// Gre header
struct greHeade
{
    struct flags
    {
        string
            checksum{},
            routing{},
            key{},
            seqNum{},
            strictSourceRoute{},
            acknowledgment{};
        string
            recursion{},
            reserved{},
            version{};
    } flags;
    string
        protocol{},
        length{},
        callID{},
        seqNum{};
};
struct pppHeader
{
    string
        address{},
        control{},
        protocol{};
};
struct frameHeader
{
    struct firstAddress
    {
        string dlci{}, cr{}, ea{};
    } firstAddress;
    struct secondAddress
    {
        string dlci{}, fecn{}, becn{}, de{}, ea{};
    } secondAddress;
    string type;
};
// Ah header
struct ahHeader
{
    string
        next{},
        length{},
        reserved{},
        spi{},
        sequence{},
        icv{};
};
// Esp header
struct espHeader
{
    string spi{}, sequence{};
};
struct vlanHeader
{
    string
        priority{},
        dei{},
        id{},
        type{};
};
// Lldp header
struct lldpHeader
{
    struct TLV
    {
        uint8_t type;
        uint16_t length;
        string value{};
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
struct dhcpHeader
{
    string
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
        string broadcast{};
        string reserved{};
    } bootpFlags;

    struct Option
    {
        string option{}, length{}, value{};
    };

    vector<Option> options{};
};
// Eigrp header
struct eigrpHeader
{
    string version{},
        opcode{},
        checksum{},
        sequence{},
        ack{},
        virtualRouterID{},
        autonomousSystem{};

    struct flags
    {
        string init{},
            conditionalRecieve{},
            restart{},
            endOfTable{};
    } flags;

    struct Option
    {
        string option{}, length{}, value{};
    };
    vector<Option> options{};
};
// Ospf header
namespace OspfPacket
{
    struct ospfHeader
    {
        string version{},
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
        string mask{},
            helloInterval{},
            routerPriority{},
            routerDeadInterval{},
            designatedRouter{},
            backupDesignatedRouter{},
            activeNeighbor{};

        struct options
        {
            string notSet{},
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
        string interfaceMtu{},
            sequence{};

        struct options
        {
            string notSet{},
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
            string OOBResync{},
                init{},
                more{},
                master{};
        } description;
    };

    struct ospfRequest
    {
        string lsType{},
            linkStatID{},
            advertisingRouter{};
    };

    struct ospfLLSHeader
    {
        string checksum{},
            dataLength{},
            options{};
    };

    struct LSA
    {
        string lsAge{},
            doNotAge{},
            lsType{},
            linkStateID{},
            advertisingRouter{},
            sequenceNumber{},
            checksum{},
            length{};

        struct options
        {
            string notSet{},
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
        string numOfLsa{};
        vector<OspfPacket::LSA> lsa{};
    };
}

// Packet structure
struct PacketInfo
{
    vector<std::any> Layer2;
    vector<std::any> Layer2_5;
    vector<std::any> Layer3;
    vector<std::any> Layer4;
    vector<std::any> Layer5;
};
// Type test
template <typename T>
bool is_type(const std::any &a)
{
    return a.type() == typeid(T);
}
