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
        std::string arp{"\x08\x06", 2};
        string ipv4{"\x08\x00", 2};
        string mpls{"\x88\x47", 2};
        string vlan{"\x86\xdd", 2};
        string lldp{"\x88\xcc", 2};
    } ethernet;
    struct arp
    {
        string ethernet = {"\x00\x01", 2};
        string ipv4 = {"\x08\x00", 2};
        struct opcode
        {
            string request{"\x00\x01", 2};
            string reply = {"\x00\x02", 2};
            string reverseRequest{"\x00\x03", 2};
            string reverseReply{"\x00\x04", 2};
            string dynamicRequest{"\x00\x05", 2};
            string dynamicReply{"\x00\x06", 2};
            string dynamicError{"\x00\x07", 2};
            string inverseRequest{"\x00\x08", 2};
            string inverseReply{"\x00\x09", 2};
            string nak{"\x00\x0a", 2};
        } opcode;
    } arp;
    struct ipv4
    {
        std::string GetValue() const { return "4"; }
        string gre{"\x2f", 1};
        string tcp{"\x06", 1};
        string udp{"\x11", 1};
        string icmp{"\x01", 1};
        string igmp{"\x02", 1};
        string ah{"\x33", 1};
        string eigrp{"\x58", 1};
    } ipv4;
    struct udp
    {
        struct dhcp
        {
            string source{"\x00\x44", 2};
            string destination{"\x00\x43", 2};
        } dhcp;
    } udp;
    struct gre
    {
        string ppp{"\x88\x0b", 2};
    } gre;
    struct ah
    {
        string esp{"\x32", 1};
    } ah;
    struct mac
    {
        string broadcast{"\xff\xff\xff\xff\xff\xff", 6};
        string source{"\x00\x00\x00\x00\x00\x00", 6};
    } mac;
    struct ip
    {
        string broadcast{"\xff\xff\xff\xff", 4};
        string source{"\x00\x00\x00\x00", 4};
    } ip;
    struct dhcp
    {
        struct type
        {
            string discover{"\x01", 1};
            string offer{"\x02", 1};
            string request{"\x03", 1};
            string ack{"\x05", 1};
            string nac{"\x06", 1};
        } type;
        struct options
        {
            string mask{"\x01", 1};
            string broadcast{"\x1c", 1};
            string timeOffset{"\x02", 1};
            string router{"\x03", 1};
            string domainName{"\x0f", 1};
            string domainServer{"\x06", 1};
            string domainSearch{"\x77", 1};
            string netbiosNameServer{"\x2c", 1};
            string netbiosScope{"\x2c", 1};
            string mtu{"\x1a", 1};
            string classlessStateRoute{"\x79", 1};
            string ntp{"\x2a", 1};
            string type{"\x35", 1};
            string hostname{"\x0c", 1};
            string clientID{"\x3d", 1};
            string serverIdentifier{"\x36", 1};
            string leaseTime{"\x33", 1};
            string renewalTime{"\x3a", 1};
            string rebindingTime{"\x3b", 1};
            string requestIP{"\x32", 1};
            string requestList{"\x37", 1};
            string maxSize{"\x39", 1};
        } options;
        string clientHardwareAddressPadding{"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 10};
        string serverHostName{"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 64} ;
        string bootfile{"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 128};
        string endPadding{"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 25};
        string end{"\xff", 1};
        string magicCookie{"\x63\x82\x53\x63", 4};
    } dhcp;
    struct eigrp
    {
        struct type
        {
            string update{"\x01", 1};
            string request{"\x02", 1};
            string query{"\x03", 1};
            string reply{"\x04", 1};
            string hello{"\x05", 1};
        } type;
        struct options
        {
            string parameter{"\x00\x01", 2};
            string version{"\x00\x04", 2};
            string sequence{"\x00\x03", 2};
            string multicastSequence{"\x00\x05", 2};
            string internalRoute{"\x01\x02", 2};
            string externalRoute{"\x01\x03", 2};
            string internalRouteV6{"\x04\02"};
            string externalRouteV6{"\x04\x03"};
            string stub{"\x00\x06", 2};
            string authentication{"\x00\x02"};
        } options;
        struct version
        {
            string release{"\x0c\x04", 2};
            string tls{"\x01\x02", 2};
        } version;
        struct externalProtocol {
            string igrp = {"\x01", 1};
            string eigrp = {"\x02", 1};
            string staticRoute = {"\x03", 1};
            string rip = {"\x04", 1};
            string hello = {"\x05", 1};
            string ospf = {"\x06", 1};
            string isis = {"\x07", 1};
            string egp = {"\x08", 1};
            string bgp = {"\x09", 1};
            string idrp = {"\x0a", 1};
            string connected = {"\x0b", 1};
        } externalProtocol;
        struct destinationAssignmentEncoding {
            string ipv4 = {"\x01", 1};
            string ipv6 = {"\x02", 1};
            string commonService{"\x40\x00", 2};
            string ipv4Family{"\x40\x01", 2};
            string ipv6Famil{"\x40\x02", 2}; 
        } destinationAssignmentEncoding;
        struct communityAttribute {
            string EXTCOMM_EIGRP{"\x00", 1};
            string EXTCOMM_DAD{"\x01", 1};
            string EXTCOMM_VRHB{"\x02", 1};
            string EXTCOMM_SRLM{"\x03", 1};
            string EXTCOMM_SAR{"\x04", 1};
            string EXTCOMM_RPM{"\x05", 1};
            string EXTCOMM_VRR{"\x06", 1};
        } communityAttribute;
    } eigrp;
    struct multicast
    {
        struct Eigrp
        {
            string address{"\xe0\x00\x00\x0a", 4};
            string mac{"\x01\x00\x5e\x00\x00\x0a", 6};
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
// Ipv6 header
struct ipv6Header
{
    
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
// Syslog Packet
struct syslogHeader {
    string PRI{},
        message{};
};

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
