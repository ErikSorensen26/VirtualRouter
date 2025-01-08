// Process.h

#ifndef PROCESS_H
#define PROCESS_H

#include <any>
#include <RoutingTable.h>
#include <ByteString.hpp>
#include <Queue.hpp>

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
     * @brief Constructs a `ProcessPacket` object with additional debugging parameters.
     *
     * This template constructor allows for passing additional arguments for debugging
     * purposes. It initializes the `data` vector with the provided arguments wrapped in `std::any`.
     * It then calls the `process` method to handle the packet.
     *
     * @tparam Args Variadic template parameters for additional debugging arguments.
     * @param packet Reference to a `PacketInfo` structure containing parsed protocol headers.
     * @param vrf Reference to a `ByteString` representing the Virtual Routing and Forwarding (VRF) context.
     * @param Interface Pointer to an `Interface` object for interacting with network interfaces.
     * @param args Additional arguments for debugging purposes.
     *
     * @note This constructor is primarily intended for debugging and testing scenarios.
     */
    template <typename... Args>
    ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface, Args... args) : interface(Interface), data{std::any(args)...} { process(packet, vrf); }

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
    void process(PacketInfo& packet, ByteString& vrf);

    /**
     * @brief Determines whether a packet header should be printed based on its type.
     *
     * This method checks if the provided `std::any` parameter matches any type
     * stored in the `data` vector. It is used to control debugging output for specific
     * protocol headers.
     *
     * @param param The `std::any` object representing a packet header.
     * @return `true` if the header type is in the `data` vector or if the `data` vector is empty;
     *         `false` otherwise.
     */
    bool print(std::any param);

private:

    Interface* interface;                  ///< Pointer to the associated `Interface` object.
    ByteString currentVrf;                 ///< Current Virtual Routing and Forwarding (VRF) context.
    
    std::vector<std::any> data{};          ///< Vector storing types of headers to be printed for debugging.
};

#endif // PROCESS_H
