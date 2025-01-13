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
class ProcessPacket {
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
    bool print = false;

    using HeaderProcessor = std::function<void(const std::any&)>; ///< Type alias for header processing functions.

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
     * @param headers The list of headers to process.
     * @param layerProcessors Map of type-indexed processing functions.
     */
    void processLayers(const std::vector<std::any>& headers, const std::unordered_map<std::type_index, HeaderProcessor>& layerProcessors);

    /**
     * @brief Initialized processing functions for different headers.
     *
     * This is called only once to populate static processor maps.
     */
    void initializeProcessors();

    /// Layer-specific header processing functions.
    void processEthernet(const Layer2Variant& header); ///< Ethernet processing function.
    void processPpp(const Layer2Variant& header);      ///< PPP processing function.
    void processArp(const Layer2_5Variant& header);      ///< ARP processing function.
    void processMpls(const std::any& header);     ///< MPLS processing function.
    void processVlan(const std::any& header);     ///< VLAN processing function.
    void processLldp(const std::any& header);     ///< LLDP processing function.
    void processIPv4(const std::any& header);     ///< IPv4 processing function.
    void processIPv6(const std::any& header);     ///< IPv6 processing function.
    void processGre(const std::any& header);      ///< GRE processing function.
    void processAh(const std::any& header);       ///< AH processing function.
    void processEsp(const std::any& header);      ///< ESP processing function.
    void processIcmp(const std::any& header);     ///< ICMP processing function.
    void processIgmp(const std::any& header);     ///< IGMP processing function.
    void processEigrp(const std::any& header);    ///< EIGRP processing function.
    void processTcp(const std::any& header);      ///< TCP processing function.
    void processUdp(const std::any& header);      ///< UDP processing function.
    void processDhcp(const std::any& header);     ///< DHCP processing function.

    static std::unordered_map<std::type_index, HeaderProcessor> layer2Processors; ///< Static Layer 2 processors.
    static std::unordered_map<std::type_index, HeaderProcessor> layer2_5Processors; ///< Static Layer 2.5 processors
    static std::unordered_map<std::type_index, HeaderProcessor> layer3Processors; ///< Static Layer 3 processors.
    static std::unordered_map<std::type_index, HeaderProcessor> layer4Processors; ///< Static Layer 4 processors.
    static std::unordered_map<std::type_index, HeaderProcessor> layer5Processors; ///< Static Layer 5 processors.
};

#endif // PROCESS_H
