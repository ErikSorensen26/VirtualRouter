#pragma once

#include <pcap.h>
#include <cstring>

#include <Functions.h>
#include <Logger.h>
#include <Interface.h>
#include "PacketStructure.h"

class Packet
{
public:
    // constructor
    Packet(ByteString &packet, bool debug, Interface& iface);

    // layers 2-3 decapsulation
    void inspection(ByteString &packet);

    // layers 4-7 decapsulation
    void decapsulate();

    // Leftover Packet
    ByteString afterPacket;

    // Packet structure initialization
    PacketInfo packetInfo;

private:

    // Interface
    Interface& currentInterface;

    // Parses and processes Ethernet header.
    void decodeEthernet(ByteString &ethernetHeader);
    // Parses and processes ARP header.
    void decodeArp(ByteString &arpHeader);
    // Parses and processes the IP header.
    void decodeIPv4(ByteString &ipv4Header, int &ipv4Size);
    // Parses and processes the IPv6 header
    void decodeIPv6(ByteString &ipv6Header);
    // Parses and processes the TCP header.
    void decodeTcp(ByteString &tcpHeader, int &tcpSize);
    // Parses and processes the UDP header.
    void decodeUdp(ByteString &udpHeader);
    // Parses and processes the ICMP header.
    void decodeIcmp(ByteString &icmpHeader);
    // Parses and processes the ICMPv6 header
    void decodeIcmpV6(ByteString &icmpV6Header);
    // Parses and processes the IGMP header.
    void decodeIgmp(ByteString &igmpHeader);
    // Parses and processes the TLS header.
    void decodeTls(ByteString &tlsHeader);
    // Parses and processes the MPLS header.
    void decodeMpls(ByteString &mplsHeader);
    // Parses and processes the GRE header.
    void decodeGre(ByteString &greHeader);
    // Parses and processes the PPP header.
    void decodePpp(ByteString &pppHeader);
    // Parses and processes the Frame Relay header.
    void decodeFrame(ByteString &frameHeader);
    // Parses and processes the AH header.
    void decodeAh(ByteString &ahHeader, int &ahSize);
    // Parses and processes the ESP header.
    void decodeEsp(ByteString &espHeader);
    // Parses and processes the VLAN header.
    void decodeVlan(ByteString &vlanHeader);
    // Parses and processes the LLDP header.
    void decodeLldp(ByteString &lldpHeader);
    // Parses and processes the DHCP header.
    void decodeDhcp(ByteString &dhcpHeader);
    // Parses and processes the EIGRP header.
    void decodeEigrp(ByteString &eigrpHeader);
    // Parses and processes the SysLog header
    void decodeSysLog(ByteString &syslogHeader);

    // Decapsulates layer 2 headers
    void l2(ByteString &packet);
    // Decapsulates layer 2.5 headers
    void l2_5(ByteString &packet);
    // Decapsulates layer 3 headers
    void l3(ByteString &packet);
    // Decapsulates layer 4 headers
    void l4(ByteString &packet);
    // Decapsulates layer 5 headers
    void l5(ByteString &packet);

    // Header Initializations
    EthernetHeader ethernet;
    ArpHeader arp;
    IPv4Header ipv4;
    IPv6Header ipv6;
    TcpHeader tcp;
    UdpHeader udp;
    IcmpHeader icmp;
    IcmpV6Header icmpv6;
    IgmpHeader igmp;
    TlsHeader tls;
    MplsHeader mpls;
    GreHeade gre;
    PppHeader ppp;
    FrameHeader frame;
    AhHeader ah;
    EspHeader esp;
    VlanHeader vlan;
    LldpHeader lldp;
    DhcpHeader dhcp;
    EigrpHeader eigrp;
    SyslogHeader syslog;

    // Packet Index
    unsigned long start{0};

    ByteString fullPacket;

    bool options;
    bool print = false;
};
