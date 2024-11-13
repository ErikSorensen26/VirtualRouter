#pragma once

#include <pcap.h>
#include <cstring>

#include <Functions.h>
#include "PacketStructure.h"

using namespace std;

class Packet
{
public:
    // constructor
    Packet(string &packet, bool debug);

    // layers 2-3 decapsulation
    void Inspection(string &packet);

    // layers 4-7 decapsulation
    void Decapsulate();

    // Leftover Packet
    string afterPacket;

    // Packet structure initialization
    PacketInfo packetInfo;

private:

    // Parses and processes Ethernet header.
    void Ethernet(string &ethernetHeader);
    // Parses and processes ARP header.
    void Arp(string &arpHeader);
    // Parses and processes the IP header.
    void Ipv4(string &ivp4Header, int &ipv4Size);
    // Parses and processes the TCP header.
    void Tcp(string &tcpHeader, int &tcpSize);
    // Parses and processes the UDP header.
    void Udp(string &udpHeader);
    // Parses and processes the ICMP header.
    void Icmp(string &icmpHeader);
    // Parses and processes the IGMP header.
    void Igmp(string &igmpHeader);
    // Parses and processes the TLS header.
    void Tls(string &tlsHeader);
    // Parses and processes the MPLS header.
    void Mpls(string &mplsHeader);
    // Parses and processes the GRE header.
    void Gre(string &greHeader);
    // Parses and processes the PPP header.
    void Ppp(string &pppHeader);
    // Parses and processes the Frame Relay header.
    void Frame(string &frameHeader);
    // Parses and processes the AH header.
    void Ah(string &ahHeader, int &ahSize);
    // Parses and processes the ESP header.
    void Esp(string &espHeader);
    // Parses and processes the VLAN header.
    void Vlan(string &vlanHeader);
    // Parses and processes the LLDP header.
    void Lldp(string &lldpHeader);
    // Parses and processes the DHCP header.
    void Dhcp(string &dhcpHeader);
    // Parses and processes the EIGRP header.
    void Eigrp(string &eigrpHeader);

    // Decapsulates layer 2 headers
    void L2(string &packet);
    // Decapsulates layer 2.5 headers
    void L2_5(string &packet);
    // Decapsulates layer 3 headers
    void L3(string &packet);
    // Decapsulates layer 4 headers
    void L4(string &packet);
    // Decapsulates layer 5 headers
    void L5(string &packet);

    // Header Initializations
    ethernetHeader ethernet;
    arpHeader arp;
    ipv4Header ipv4;
    tcpHeader tcp;
    udpHeader udp;
    icmpHeader icmp;
    igmpHeader igmp;
    tlsHeader tls;
    mplsHeader mpls;
    greHeade gre;
    pppHeader ppp;
    frameHeader frame;
    ahHeader ah;
    espHeader esp;
    vlanHeader vlan;
    lldpHeader lldp;
    dhcpHeader dhcp;
    eigrpHeader eigrp;

    Variable variable;

    // Packet Index
    unsigned long start{0};

    string fullPacket;

    bool options;
    bool print = false;
};
