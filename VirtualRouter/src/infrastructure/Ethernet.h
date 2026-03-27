/**
 * @file Ethernet.h
 * @brief Raw Ethernet frame construction helpers for the infrastructure layer.
 */

/**
 * @defgroup INFRASTRUCTURE Network Infrastructure
 * @brief ARP/NDP cache, Ethernet framing, IP packet processing.
 */

#ifndef ETHERNET_H
#define ETHERNET_H

#include <cstdint>

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

/**
 * @namespace infrastructure::ethernet
 * @brief Helpers for constructing and sending raw Ethernet frames.
 * @ingroup INFRASTRUCTURE
 */
namespace infrastructure::ethernet
{
    /**
     * @brief Construct a complete Ethernet header and enqueue the frame for TX.
     *
     * Fills the Ethernet destination/source MAC fields and EtherType, then
     * writes the result into @p packetInfo's current build slot.
     *
     * @param iface     Interface supplying the source MAC address.
     * @param packetInfo PacketBuilder that holds the frame buffer.
     * @param destMac   Destination MAC address (48-bit packed into 64 bits, network order).
     * @param type      EtherType value (e.g. 0x0800 for IPv4, 0x86DD for IPv6).
     * @return True if the header was written successfully; false on buffer overflow.
     */
    bool build(interface::Interface* iface, processing::PacketBuilder& packetInfo, uint64_t destMac, uint16_t type);

    /**
     * @brief Reserve space in @p packetInfo for an Ethernet header without filling it.
     *
     * Used when the destination MAC is not yet known (e.g. pending ARP resolution).
     * The caller must fill the reserved region before calling send().
     *
     * @param packetInfo PacketBuilder to reserve space in.
     * @return True if reservation succeeded; false if the buffer is full.
     */
    bool reserve(processing::PacketBuilder& packetInfo);
} // namespace infrastructure::ethernet

#endif // ETHERNET_H

