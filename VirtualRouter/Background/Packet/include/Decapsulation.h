// Decapsulation.h

#ifndef DECAPSULATION_H
#define DECAPSULATION_H

#include <pcap.h>
#include <cstring>

#include <Functions.h>
#include <Logger.h>
#include <Interface.h>
#include "PacketStructure.h"

/**
 * @class Packet
 * @brief Represents a network packet and handles its decapsulation across various protocol layers.
 *
 * The Packet class is responsible for inspecting and decapsulating network packets through layers 2 to 5.
 * It parses headers for protocols such as Ethernet, ARP, IPv4/IPv6, TCP/UDP, ICMP/ICMPv6, IGMP, TLS,
 * MPLS, GRE, PPP, Frame Relay, AH, ESP, VLAN, LLDP, DHCP, EIGRP, SysLog, ctr..
 *
 * It utilizes the Singleton design pattern to ensure a single instance per packet processing.
 * Thread safety is maintained through the use of mutexes and proper synchronization mechanisms.
 */
class Packet
{
public:

    /**
     * @brief Constructs a Packet object and initiates packet inspection.
     *
     * @param packet The ByteString representation of the raw packet data.
     * @param debug Boolean flag to enable or disable debug logging.
     * @param iface Reference to the Interface object associated with the packet.
     */
    Packet(ByteString &packet, bool debug, Interface& iface);

    /**
     * @brief Inspects the given packet and processes each protocol layer (Layers 2-3).
     *
     * @param packet The ByteString representation of the raw packet data to inspect.
     */
    void inspection(ByteString &packet);

    /**
     * @brief Decapsulates the remaining protocol layers (Layers 4-5) after initial inspection.
     */
    void decapsulate();

    /**
     * @brief Holds the leftover packet data after processing.
     *
     * This ByteString contains the portion of the packet that remains unprocessed after decapsulation.
     */
    ByteString afterPacket;

    /**
     * @brief Contains the structured information of the packet after decapsulation.
     *
     * The PacketInfo struct aggregates information from various protocol layers for easy access and analysis.
     */
    PacketInfo packetInfo;

private:

    /**
     * @brief Reference to the Interface associated with this packet.
     *
     * Ensures that the packet is processed in the context of the correct network interface.
     */
    Interface& currentInterface;

    // Decapsulation and Decoding Methods

    /**
     * @brief Parses and processes the Ethernet header.
     *
     * @param ethernetHeader The ByteString representation of the Ethernet header.
     */
    void decodeEthernet(ByteString &ethernetHeader);

    /**
     * @brief Parses and processes the ARP header.
     *
     * @param arpHeader The ByteString representation of the ARP header.
     */
    void decodeArp(ByteString &arpHeader);

    /**
     * @brief Parses and processes the IPv4 header.
     *
     * @param ipv4Header The ByteString representation of the IPv4 header.
     * @param ipv4Size Reference to an integer indicating the size of the IPv4 header.
     */
    void decodeIPv4(ByteString &ipv4Header, size_t &ipv4Size);

    /**
     * @brief Parses and processes the IPv6 header.
     *
     * @param ipv6Header The ByteString representation of the IPv6 header.
     */
    void decodeIPv6(ByteString &ipv6Header);

    /**
     * @brief Parses and processes the TCP header.
     *
     * @param tcpHeader The ByteString representation of the TCP header.
     * @param tcpSize Reference to an integer indicating the size of the TCP header.
     */
    void decodeTcp(ByteString &tcpHeader, size_t &tcpSize);

    /**
     * @brief Parses and processes the UDP header.
     *
     * @param udpHeader The ByteString representation of the UDP header.
     */
    void decodeUdp(ByteString &udpHeader);

    /**
     * @brief Parses and processes the ICMP header.
     *
     * @param icmpHeader The ByteString representation of the ICMP header.
     */
    void decodeIcmp(ByteString &icmpHeader);

    /**
     * @brief Parses and processes the ICMPv6 header.
     *
     * @param icmpV6Header The ByteString representation of the ICMPv6 header.
     */
    void decodeIcmpV6(ByteString &icmpV6Header);

    /**
     * @brief Parses and processes the IGMP header.
     *
     * @param igmpHeader The ByteString representation of the IGMP header.
     *
     * @note Currently, the implementation is commented out and requires completion.
     */
    void decodeIgmp(ByteString &igmpHeader);

    void decodeTls(ByteString &tlsHeader);

    void decodeMpls(ByteString &mplsHeader);

    /**
     * @brief Parses and processes the GRE header.
     *
     * @param greHeader The ByteString representation of the GRE header.
     */
    void decodeGre(ByteString &greHeader);

    /**
     * @brief Parses and processes the PPP header.
     *
     * @param pppHeader The ByteString representation of the PPP header.
     */
    void decodePpp(ByteString &pppHeader);

    /**
     * @brief Parses and processes the Frame Relay header.
     *
     * @param frameHeader The ByteString representation of the Frame Relay header.
     */
    void decodeFrame(ByteString &frameHeader);

    /**
     * @brief Parses and processes the AH header.
     *
     * @param ahHeader The ByteString representation of the AH header.
     * @param ahSize Reference to an integer indicating the size of the AH header.
     */
    void decodeAh(ByteString &ahHeader, size_t &ahSize);

    /**
     * @brief Parses and processes the ESP header.
     *
     * @param espHeader The ByteString representation of the ESP header.
     */
    void decodeEsp(ByteString &espHeader);

    /**
     * @brief Parses and processes the VLAN header.
     *
     * @param vlanHeader The ByteString representation of the VLAN header.
     */
    void decodeVlan(ByteString &vlanHeader);

    /**
     * @brief Parses and processes the LLDP header.
     *
     * @param lldpHeader The ByteString representation of the LLDP header.
     */
    void decodeLldp(ByteString &lldpHeader);

    /**
     * @brief Parses and processes the DHCP header.
     *
     * @param dhcpHeaders The ByteString representation of the DHCP headers.
     */
    void decodeDhcp(ByteString &dhcpHeader);

    /**
     * @brief Parses and processes the EIGRP header.
     *
     * @param eigrpHeader The ByteString representation of the EIGRP header.
     */
    void decodeEigrp(ByteString &eigrpHeader);

    /**
     * @brief Parses and processes the SysLog header.
     *
     * @param syslogHeader The ByteString representation of the SysLog header.
     */
    void decodeSysLog(ByteString &syslogHeader);

    // Decapsulation Layer Methods

    /**
     * @brief Decapsulates layer 2 headers from the packet.
     *
     * @param packet The ByteString representation of the packet data.
     */
    void l2(ByteString &packet);

    /**
     * @brief Decapsulates layer 2.5 headers from the packet (e.g., VLAN, MPLS).
     *
     * @param packet The ByteString representation of the packet data.
     */
    void l2_5(ByteString &packet);

    /**
     * @brief Decapsulates layer 3 headers from the packet.
     *
     * @param packet The ByteString representation of the packet data.
     */
    void l3(ByteString &packet);

    /**
     * @brief Decapsulates layer 4 headers from the packet.
     *
     * @param packet The ByteString representation of the packet data.
     */
    void l4(ByteString &packet);

    /**
     * @brief Decapsulates layer 5 headers from the packet (e.g., DHCP).
     *
     * @param packet The ByteString representation of the packet data.
     */
    void l5(ByteString &packet);

    // Header Initializations
    std::shared_ptr<EthernetHeader> ethernet;   ///< Holds the Ethernet header information.
    std::shared_ptr<ArpHeader> arp;             ///< Holds the ARP header information.
    std::shared_ptr<IPv4Header> ipv4;           ///< Holds the IPv4 header information.
    std::shared_ptr<IPv6Header> ipv6;           ///< Holds the IPv6 header information.
    std::shared_ptr<TcpHeader> tcp;             ///< Holds the TCP header information.
    std::shared_ptr<UdpHeader> udp;             ///< Holds the UDP header information.
    std::shared_ptr<IcmpHeader> icmp;           ///< Holds the ICMP header information.
    std::shared_ptr<IcmpV6Header> icmpv6;       ///< Holds the ICMPv6 header information.
    std::shared_ptr<IgmpHeader> igmp;           ///< Holds the IGMP header information.
    std::shared_ptr<TlsHeader> tls;             ///< Holds the TLS header information.
    std::shared_ptr<MplsHeader> mpls;           ///< Holds the MPLS header information.
    std::shared_ptr<GreHeade> gre;              ///< Holds the GRE header information.
    std::shared_ptr<PppHeader> ppp;             ///< Holds the PPP header information.
    std::shared_ptr<FrameHeader> frame;         ///< Holds the Frame Relay information.
    std::shared_ptr<AhHeader> ah;               ///< Holds the AH header information.
    std::shared_ptr<EspHeader> esp;             ///< Holds the ESP header information.
    std::shared_ptr<VlanHeader> vlan;           ///< Holds the VLAN header information.
    std::shared_ptr<LldpHeader> lldp;           ///< Holds the LLDP header information.
    std::shared_ptr<DhcpHeader> dhcp;           ///< Holds the DHCP header information.
    std::shared_ptr<EigrpHeader> eigrp;         ///< Holds the EIGRP header information.
    std::shared_ptr<SyslogHeader> syslog;       ///< Holds the SysLog header information.

    size_t start{0};            ///< Index indicating the current position within the packet data.
    ByteString fullPacket;      ///< Holds the complete packet data for processing.
    bool options;               ///< Boolean flag indicating whether additional options are present.
    bool print = false;         ///< Boolesn flag to control debug printing.
};

#endif // DECAPSULATION_H
