// Decapsulation.h

#ifndef DECAPSULATION_H
#define DECAPSULATION_H

#include <pcap.h>
#include <cstring>

#include <Functions.h>
#include <Logger.h>
#include <Interface.h>
#include <algorithm>
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
    friend class TestablePacket;

    /**
     * @brief Constructs a Packet object and initiates packet inspection.
     *
     * @param packet The ByteString representation of the raw packet data.
     * @param debug Boolean flag to enable or disable debug logging.
     * @param iface Reference to the Interface object associated with the packet.
     */
    Packet(ByteString &packet, bool debug, Interface& iface);

    /**
     * @brief Constructs a Packet object without starting processing.
     */
    Packet(ByteString& packet);

    /**
     * @brief Destructor for the Packet class
     */
    ~Packet() {}

    /**
     * @brief Inspects the given packet and processes each protocol layer (Layers 2-3).
     *
     * @param packet The ByteString representation of the raw packet data to inspect.
     */
    bool inspection(const ByteString &packet);

    /**
     * @brief Decapsulates the remaining protocol layers (Layers 4-5) after initial inspection.
     */
    bool decapsulate();

    ByteString afterPacket;  ///< Remaining packet data after processing.
    PacketInfo packetInfo;   ///< Structured packet data.

// Inspects the given packet and processes each layer.

protected:
    Interface* currentInterface;    ///< Reference to the current Interface.

    size_t start{0};            ///< Index indicating the current position within the packet data.
    ByteString fullPacket;      ///< Holds the complete packet data for processing.
    bool options;               ///< Boolean flag indicating whether additional options are present.
    bool print = false;         ///< Boolesn flag to control debug printing.

    // Header search for layer 2
    template <typename HeaderType>
    const HeaderType* getLayer2Header() const {
        for (const auto& headerVariant : packetInfo.Layer2) { if (auto ptr = std::get_if<HeaderType>(&headerVariant)) { return ptr; }} return nullptr;
    }

    // Header search for layer 2.5
    template <typename HeaderType>
    const HeaderType* getLayer2_5Header() const {
        for (const auto& headerVariant : packetInfo.Layer2_5) { if (auto ptr = std::get_if<HeaderType>(&headerVariant)) { return ptr; }} return nullptr;
    }

    // Header search for layer 3
    template <typename HeaderType>
    const HeaderType* getLayer3Header() const {
        for (const auto& headerVariant : packetInfo.Layer3) { if (auto ptr = std::get_if<HeaderType>(&headerVariant)) { return ptr; }} return nullptr;
    }
    
    // Header search for layer 4
    template <typename HeaderType>
    const HeaderType* getLayer4Header() const {
        for (const auto& headerVariant : packetInfo.Layer4) { if (auto ptr = std::get_if<HeaderType>(&headerVariant)) { return ptr; }}return nullptr;
    }

    // Header search for layer 5
    template <typename HeaderType>
    const HeaderType* getLayer5Header() const {
        for (const auto& headerVariant : packetInfo.Layer5) { if (auto ptr = std::get_if<HeaderType>(&headerVariant)) { return ptr; }} return nullptr;
    }
    
    template <typename HeaderType, typename VariantType>
    const HeaderType* findFirstHeader(const std::vector<VariantType>& layer) const {
        for (const auto& headerVariant : layer) { if (auto ptr = std::get_if<HeaderType>(&headerVariant)) { return ptr; }} return nullptr;
    }

    template <typename HeaderType, typename VariantType>
    const HeaderType* findHeader(const std::vector<VariantType>& layer) const {
        auto it = std::find_if(layer.begin(), layer.end(), [](const VariantType& var) -> bool { return std::holds_alternative<HeaderType>(var); });
        if (it != layer.end()) { return &std::get<HeaderType>(*it); } return nullptr;
    }

    template <typename T>
    bool processLayer(ByteString& packet, T& decoder, size_t headerSize);
    ByteString getSlice(size_t length);



//--------------------------------------------------------------------------------
// Full Layer Methods
//--------------------------------------------------------------------------------

    /**
     * @brief Decapsulates layer 2 headers from the packet.
     *
     * @param packet The ByteString representation of the packet data.
     *
     * @return bool Indicating whether process was successfull.
     */
    bool processLayer2(const ByteString &packet);

    /**
     * @brief Decapsulates layer 2.5 headers from the packet (e.g., VLAN, MPLS).
     *
     * @param packet The ByteString representation of the packet data.
     *
     * @return bool Indicating whether process was successfull.
     */
    bool processLayer2_5(const ByteString &packet);

    /**
     * @brief Decapsulates layer 3 headers from the packet.
     *
     * @param packet The ByteString representation of the packet data.
     *
     * @return bool Indicating whether process was successfull.
     */
    bool processLayer3(const ByteString &packet);

    /**
     * @brief Decapsulates layer 4 headers from the packet.
     *
     * @param packet The ByteString representation of the packet data.
     *
     * @return bool Indicating whether process was successfull.
     */
    bool processLayer4(const ByteString &packet);

    /**
     * @brief Decapsulates layer 5 headers from the packet (e.g., DHCP).
     *
     * @param packet The ByteString representation of the packet data.
     *
     * @return bool Indicating whether process was successfull.
     */
    bool processLayer5(const ByteString &packet);

//--------------------------------------------------------------------------------
// Decapsulation and Decoding Methods
//--------------------------------------------------------------------------------

    /**
     * @brief Parses and processes the Ethernet header.
     *
     * @param ethernetHeader The ByteString representation of the Ethernet header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeEthernet(ByteString &ethernetHeader);

    /**
     * @brief Parses and processes the ARP header.
     *
     * @param arpHeader The ByteString representation of the ARP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeArp(ByteString &arpHeader);

    /**
     * @brief Parses and processes the IPv4 header.
     *
     * @param ipv4Header The ByteString representation of the IPv4 header.
     * @param ipv4Size Reference to an integer indicating the size of the IPv4 header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeIPv4(ByteString &ipv4Header, size_t &ipv4Size);

    /**
     * @brief Parses and processes the IPv6 header.
     *
     * @param ipv6Header The ByteString representation of the IPv6 header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeIPv6(ByteString &ipv6Header);

    /**
     * @brief Parses and processes the TCP header.
     *
     * @param tcpHeader The ByteString representation of the TCP header.
     * @param tcpSize Reference to an integer indicating the size of the TCP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeTcp(ByteString &tcpHeader, size_t &tcpSize);

    /**
     * @brief Parses and processes the UDP header.
     *
     * @param udpHeader The ByteString representation of the UDP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeUdp(ByteString &udpHeader);

    /**
     * @brief Parses and processes the ICMP header.
     *
     * @param icmpHeader The ByteString representation of the ICMP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeIcmp(ByteString &icmpHeader);

    /**
     * @brief Parses and processes the ICMPv6 header.
     *
     * @param icmpV6Header The ByteString representation of the ICMPv6 header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeIcmpV6(ByteString &icmpV6Header);

    /**
     * @brief Parses and processes the IGMP header.
     *
     * @param igmpHeader The ByteString representation of the IGMP header.
     *
     * @return bool Indicating whether decode was successful.
     *
     * @note Currently, the implementation is commented out and requires completion.
     */
    bool decodeIgmp(ByteString &igmpHeader);

    /**
     * @brief Parses and processes the TLS header.
     *
     * @param tlsHeader The ByteString representation of the TLS header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeTls(ByteString &tlsHeader);

    /**
     * @brief Parses and processes the MPLS header.
     *
     * @param mplsHeader The ByteString representation of the MPLS header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeMpls(ByteString &mplsHeader);

    /**
     * @brief Parses and processes the GRE header.
     *
     * @param greHeader The ByteString representation of the GRE header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeGre(ByteString &greHeader);

    /**
     * @brief Parses and processes the PPP header.
     *
     * @param pppHeader The ByteString representation of the PPP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodePpp(ByteString &pppHeader);

    /**
     * @brief Parses and processes the Frame Relay header.
     *
     * @param frameHeader The ByteString representation of the Frame Relay header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeFrame(ByteString &frameHeader);

    /**
     * @brief Parses and processes the AH header.
     *
     * @param ahHeader The ByteString representation of the AH header.
     * @param ahSize Reference to an integer indicating the size of the AH header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeAh(ByteString &ahHeader, size_t &ahSize);

    /**
     * @brief Parses and processes the ESP header.
     *
     * @param espHeader The ByteString representation of the ESP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeEsp(ByteString &espHeader);

    /**
     * @brief Parses and processes the VLAN header.
     *
     * @param vlanHeader The ByteString representation of the VLAN header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeVlan(ByteString &vlanHeader);

    /**
     * @brief Parses and processes the LLDP header.
     *
     * @param lldpHeader The ByteString representation of the LLDP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeLldp(ByteString &lldpHeader);

    /**
     * @brief Parses and processes the DHCP header.
     *
     * @param dhcpHeaders The ByteString representation of the DHCP headers.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeDhcp(ByteString &dhcpHeader);

    /**
     * @brief Parses and processes the EIGRP header.
     *
     * @param eigrpHeader The ByteString representation of the EIGRP header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeEigrp(ByteString &eigrpHeader);

    /**
     * @brief Parses and processes the SysLog header.
     *
     * @param syslogHeader The ByteString representation of the SysLog header.
     *
     * @return bool Indicating whether decode was successful.
     */
    bool decodeSysLog(ByteString &syslogHeader);
};

#endif // DECAPSULATION_H
