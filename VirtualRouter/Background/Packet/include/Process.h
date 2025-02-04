// Process.h

#ifndef PROCESS_H
#define PROCESS_H

#include <RoutingTable.h>
#include <ByteString.hpp>
#include <Queue.hpp>
#include <any>
#include <unordered_map>
#include <functional>
#include <typeindex>

/**
 * @file Process.h
 * @brief Defines the ProcessPacket class for processing captured network packets.
 */

struct HeaderVisitor;
class Interface;

/**
 * @class ProcessPacket
 * @brief Handles processing of captured network packets across various protocol layers.
 *
 * The `ProcessPacket` class is responsible for analyzing and handling network packets
 * by examining different protocol headers (Layer 2 to Layer 5). It interacts with
 * the `Interface` class to manage ARP replies, update routing tables, and handle DHCP
 * and EIGRP operations.
 *
 * @note Ensure that the `Interface` object provided is valid and properly initialized
 *       before using the `ProcessPacket` class to avoid undefined behavior.
 */
class ProcessPacket 
{
public:
    /**
     * @brief Constructs a `ProcessPacket` object.
     *
     * This constructor initializes the `ProcessPacket` object with the provided `Interface`
     * and processes the given packet within the specified VRF context.
     *
     * @param packet Reference to a `PacketInfo` structure containing parsed protocol headers.
     * @param vrf Reference to a `ByteString` representing the Virtual Routing and Forwarding (VRF) context.
     * @param Interface Pointer to an `Interface` object for interacting with network interfaces.
     *
     * @note Ensure that the `Interface` object is valid and properly initialized before using this constructor.
     */
    ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface);

private:
    Interface* interface;                   ///< Pointer to the associated `Interface` object.
    ByteString currentVrf;                  ///< Current Virtual Routing and Forwarding (VRF) context.
    std::unordered_map<std::type_index, std::any> parsedHeaders; ///< Shared map of parsed headers.
    ByteString macAddress;                  ///< MAC Address found in packet.
    PacketInfo& currentPacket;              ///< Packet being processed.
    bool print = false;                     ///< Debug flag.

    friend struct HeaderVisitor;
    
    // Header storage using std::variant
    std::vector<Layer2Variant> layer2;
    std::vector<Layer2_5Variant> layer2_5;
    std::vector<Layer3Variant> layer3;
    std::vector<Layer4Variant> layer4;
    std::vector<Layer5Variant> layer5;

    /// Layer-specific header processing functions.
    void processEthernet(const EthernetHeader& header);     ///< Ethernet processing function.
    void processPpp(const PppHeader& header);               ///< PPP processing function.
    void processFrame(const FrameHeader& header);           ///< Frame processing function.

    void processArp(const ArpHeader& header);               ///< ARP processing function.
    void processMpls(const MplsHeader& header);             ///< MPLS processing function.
    void processVlan(const VlanHeader& header);             ///< VLAN processing function.
    void processLldp(const LldpHeader& header);             ///< LLDP processing function.

    void processIPv4(const IPv4Header& header);             ///< IPv4 processing function.
    void processIPv6(const IPv6Header& header);             ///< IPv6 processing function.
    void processGre(const GreHeade& header);                ///< GRE processing function.
    void processAh(const AhHeader& header);                 ///< AH processing function.
    void processEsp(const EspHeader& header);               ///< ESP processing function.
    void processIcmp(const IcmpHeader& header);             ///< ICMP processing function.
    void processIcmpV6(const IcmpV6Header& header);         ///< ICMP processing function.
    void processIgmp(const IgmpHeader& header);             ///< IGMP processing function.

    void processTcp(const TcpHeader& header);               ///< TCP processing function.
    void processUdp(const UdpHeader& header);               ///< UDP processing function.
    void processEigrp(const EigrpHeader& header);           ///< EIGRP processing function.

    void processDhcp(const DhcpHeader& header);             ///< DHCP processing function.

    /**
     * @brief Processes the captured packet by examining and handling various header types.
     *
     * This method iterates through different protocol layers (Layer 2 to Layer 5),
     * identifies the type of each header, and performs appropriate actions such as
     * sending ARP replies, updating routing tables, and handling DHCP and EIGRP operations.
     *
     * @param packet Reference to a `PacketInfo` structure containing parsed protocol headers.
     * @param vrf Reference to a `ByteString` representing the Virtual Routing and Forwarding (VRF) context.
     */
    void process(PacketInfo& packet);

    /**
     * @brief Processes header information for a given protocol layer.
     *
     * Iterates through headers and applies the appropriate processing function.
     *
     * @param VarientType The std::variant type for the layer.
     * @param headers The vector of headers to process.
     */
    template<typename VariantType>
    void processLayer(const std::vector<VariantType>& headers);

    HeaderVisitor* visitor; //< Visitor struct to handle std::visit.
};

/**
 * @struct HeaderVisitor
 * @brief Visitor struct to handle different header types using std::visit.
 *
 * This struct overloads the operator() for each header type, allowing
 * std::visit to call the appropriate processing function.
 */
struct HeaderVisitor {
    ProcessPacket* processor;

    void operator()(const EthernetHeader& eth) const { processor->processEthernet(eth); }
    void operator()(const PppHeader& ppp) const { processor->processPpp(ppp); }
    void operator()(const FrameHeader& frame) const { processor->processFrame(frame); }

    void operator()(const ArpHeader& arp) const { processor->processArp(arp); }
    void operator()(const MplsHeader& mpls) const { processor->processMpls(mpls); }
    void operator()(const VlanHeader& vlan) const { processor->processVlan(vlan); }
    void operator()(const LldpHeader& lldp) const { processor->processLldp(lldp); }

    void operator()(const IPv4Header& ipv4) const { processor->processIPv4(ipv4); }
    void operator()(const IPv6Header& ipv6) const { processor->processIPv6(ipv6); }
    void operator()(const GreHeade& gre) const { processor->processGre(gre); }
    void operator()(const AhHeader& ah) const { processor->processAh(ah); }
    void operator()(const EspHeader& esp) const { processor->processEsp(esp); }
    void operator()(const IcmpHeader& icmp) const { processor->processIcmp(icmp); }
    void operator()(const IcmpV6Header& icmp) const { processor->processIcmpV6(icmp); }
    void operator()(const IgmpHeader& igmp) const { processor->processIgmp(igmp); }

    void operator()(const TcpHeader& tcp) const { processor->processTcp(tcp); }
    void operator()(const UdpHeader& udp) const { processor->processUdp(udp); }
    void operator()(const EigrpHeader& eigrp) const { processor->processEigrp(eigrp); }

    void operator()(const DhcpHeader& dhcp) const { processor->processDhcp(dhcp); }
};

#endif // PROCESS_H
