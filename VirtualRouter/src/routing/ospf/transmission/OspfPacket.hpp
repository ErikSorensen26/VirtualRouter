/**
 * @file OspfPacket.hpp
 * @brief Unicast OSPF packet wrapper holding a serialized buffer and its destination address.
 */

#ifndef OSPF_PACKET_HPP
#define OSPF_PACKET_HPP

#include <cstring>
#include <IPAddress.h>

#include "packet/StaticHeader.hpp"

namespace routing::ospf
{
class Neighbor;

/**
 * @brief A fully serialized OSPF packet queued for unicast transmission.
 * @ingroup OSPF_TRANSMISSION
 *
 * Bundles a @ref packet::StaticHeader (fixed-size raw buffer), the destination IP
 * address, and a sequence number used by the retransmission machinery. Instances are
 * stored in per-neighbor retransmission lists and pacing queues.
 *
 * ## Lifecycle & Ownership
 * Owned exclusively by retransmission lists or pacing queues inside the owning
 * @ref PacketDispatcher. The dispatcher constructs, stores, and discards them.
 *
 * @see RetransmissionList, PacketDispatcher
 */
class UnicastPacket
{
public:
    UnicastPacket() = default;

    /**
     * @brief Constructs a unicast packet from a serialized buffer and destination.
     *
     * @param buf  Pointer to the serialized OSPF packet bytes.
     * @param len  Length of the packet in bytes.
     * @param dest Destination IP address (unicast neighbor address).
     */
    UnicastPacket(uint8_t* buf, size_t len, const types::IPAddress& dest)
        : packet(buf, len), destination(dest) {}

    // Default copy constructor and assignment operator
    UnicastPacket(const UnicastPacket&) = default;
    UnicastPacket& operator=(const UnicastPacket&) = default;

    // Default move constructor and move assignment operator
    UnicastPacket(UnicastPacket&&) = default;
    UnicastPacket& operator=(UnicastPacket&&) = default;

    /**
     * @brief Resets this packet to an empty state and zeroes the sequence number.
     *
     * Used to clear a retransmission slot when the ACK is received.
     */
    void reset()
    {
        packet = packet::StaticHeader{};
        sequence = 0;
    }

    uint32_t sequence; ///< Monotonic retransmission sequence number; used to detect stale ACKs.

    packet::StaticHeader packet;    ///< Serialized packet bytes.
    types::IPAddress destination;   ///< Unicast destination IP (neighbor's interface address).
};
} // namespace routing

#endif

