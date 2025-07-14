// Egress.h

#ifndef EGRESS_H
#define EGRESS_H

#include <pcap.h>
#include <string>
#include <vector>

/**
 * @class Egress
 * @brief Handles sending network packets through a specified network interface.
 *
 * The Egress class is responsible for sending network packets by interfacing with
 * the libpcap library. It converts packet data from a base256 string format to
 * raw bytes and transmits them over the designated network interface.
 *
 * @note Ensure that the network interface provided exists and is up before using
 *       the Egress class to avoid runtime errors.
 */
class Egress 
{
public:
    
    /**
     * @brief Constructs an Egress object and initializes the pcap handle.
     *
     * This constructor opens a live capture handle on the specified network interface
     * for sending packets. If the interface cannot be opened, it throws a runtime
     * exception with an appropriate error message.
     *
     * @param interface The name of the network interface (e.g., "eth0") to send packets through.
     *
     * @throws std::runtime_error if the pcap handle cannot be opened.
     */
    Egress(const std::string& interface);
    
    /**
     * @brief Destructs the Egress object and closes the pcap handle.
     *
     * The destructor ensures that the pcap handle is properly closed to release
     * any associated resources.
     */
    ~Egress();

    /**
     * @brief Sends a network packet using the provided base256 string.
     *
     * This method converts the base256 string representation of a packet into raw
     * bytes and sends it through the initialized network interface. It returns
     * `true` if the packet is sent successfully, and `false` otherwise.
     *
     * @param base256Str The ByteString containing the packet data in base256 string format.
     * @return `true` if the packet is sent successfully; `false` otherwise.
     *
     * @note Ensure that the packet data is correctly formatted to match the expected protocol.
     */
    bool sendPacket(const uint8_t* base256Str, size_t size);

private:

    /**
     * @brief Holds the pcap handle for sending packets.
     *
     * The `pcap_handle` is used by libpcap functions to interact with the network interface.
     */
    pcap_t* pcap_handle;
};

#endif // EGRESS_H
