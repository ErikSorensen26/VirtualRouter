/**
 * @file ReliablePacket.hpp
 * @brief EIGRP reliable transport: sequenced packet with retransmission state.
 */

#ifndef RELIABLE_PACKET_HPP
#define RELIABLE_PACKET_HPP

#include <cstring>
#include <chrono>
#include <IPAddress.h>

#include "packet/headers/EigrpHeader.hpp"
#include "packet/StaticHeader.hpp"

namespace routing::eigrp
{
class Neighbor;

/**
 * @brief Per-transmission bookkeeping for a single reliable packet send attempt.
 * @ingroup EIGRP_RTP
 *
 * Attached to every in-flight reliable packet (unicast or multicast) and tracks
 * the timing and retry state needed by the RTO algorithm. A separate
 * `ReliableInfo` is maintained per neighbor for multicast packets because each
 * neighbor's acknowledgment arrives independently.
 *
 * @see UnicastReliablePacket
 * @see MulticastReliablePacket
 */
struct ReliableInfo
{
    std::chrono::steady_clock::time_point sendTime; ///< Timestamp of the most recent transmission attempt; used to compute RTT samples.
    uint8_t retransmissionCount = 0; ///< Number of retransmission attempts so far; peer is declared dead at @ref MAX_RETRANSMISSIONS.
    uint32_t timerId = 0; ///< Handle of the pending retransmission timer; 0 means no timer is armed.
    uint32_t sequence = 0; ///< Sequence number assigned to this transmission.
};

/**
 * @brief A reliable EIGRP packet destined for a single neighbor.
 * @ingroup EIGRP_RTP
 *
 * Captures the serialized packet bytes and the destination IP at the moment
 * of transmission. The packet bytes are frozen via @ref packet::StaticHeader
 * so retransmissions replay the exact original wire content without
 * re-serialization.
 *
 * ## Lifecycle & Ownership
 * Created by @ref ReliableTransport when a unicast reliable send is initiated.
 * Stored in `Neighbor::reliablePackets` (keyed by sequence number) until the
 * corresponding ACK is received or the neighbor is declared dead.
 *
 * @see ReliableTransport::startUnicastReliable
 * @see Neighbor
 */
class UnicastReliablePacket
{
public:
    UnicastReliablePacket() = default;

    /**
     * @brief Constructs from a completed packet header and its destination.
     *
     * Snapshots the serialized bytes from `builder` (fixed header + trailing
     * TLV payload) into an immutable @ref packet::StaticHeader so the packet
     * can be retransmitted without re-encoding.
     *
     * @param builder Completed EIGRP header containing the serialized packet.
     * @param dest    Unicast IP address of the target neighbor.
     */
    UnicastReliablePacket(packet::EigrpHeader& builder, const types::IPAddress& dest)
        : packet(builder.buffer, builder.fixedSize + builder.getTrail().size()), destination(dest) {}

    // Default copy constructor and copy assignment operator
    UnicastReliablePacket(const UnicastReliablePacket&) = default;
    UnicastReliablePacket& operator=(const UnicastReliablePacket&) = default;

    // Default move constructor and move assignment operator
    UnicastReliablePacket(UnicastReliablePacket&&) = default;
    UnicastReliablePacket& operator=(UnicastReliablePacket&&) = default;

    ReliableInfo info;           ///< Timing and retry state for this transmission.
    packet::StaticHeader packet; ///< Frozen serialized packet bytes.
    types::IPAddress destination;
};

/**
 * @brief A reliable EIGRP packet originally sent to the multicast group.
 * @ingroup EIGRP_RTP
 *
 * Multicast reliable packets are sent once to the group address but require
 * an ACK from each neighbor that received them. A per-neighbor @ref ReliableInfo
 * is tracked so that unicast retransmissions can be sent individually to any
 * neighbor that has not yet acknowledged.
 *
 * ## Lifecycle & Ownership
 * Created by @ref ReliableTransport and stored in
 * `ReliableTransport::reliablePackets` (keyed by sequence number) until every
 * neighbor in `neighbors` has acknowledged or been declared dead.
 *
 * @see ReliableTransport::startMulticastReliable
 * @see ReliableTransport::handleRetransmission
 */
class MulticastReliablePacket
{
public:
    MulticastReliablePacket() = default;

    /**
     * @brief Constructs from a completed packet header and an initial neighbor set.
     *
     * Snapshots the serialized bytes from `builder` and takes ownership of the
     * per-neighbor `ReliableInfo` map, which was populated with sequence numbers
     * and send timestamps by the caller before construction.
     *
     * @param builder Completed EIGRP header containing the serialized packet.
     * @param nbrs    Map from neighbor pointer to its individual @ref ReliableInfo;
     *                ownership is moved into this object.
     */
    MulticastReliablePacket(packet::EigrpHeader& builder, std::unordered_map<Neighbor*, ReliableInfo>& nbrs)
        : packet(builder.buffer, builder.fixedSize + builder.getTrail().size()), neighbors(std::move(nbrs)) {}

    // Default copy constructor and copy assignment operator
    MulticastReliablePacket(const MulticastReliablePacket&) = default;
    MulticastReliablePacket& operator=(const MulticastReliablePacket&) = default;

    // Default move constructor and move assignment operator
    MulticastReliablePacket(MulticastReliablePacket&&) = default;
    MulticastReliablePacket& operator=(MulticastReliablePacket&&) = default;

    packet::StaticHeader packet; ///< Frozen serialized packet bytes, identical for all retransmissions.
    std::unordered_map<Neighbor*, ReliableInfo> neighbors; ///< Per-neighbor retransmission state; entry is removed when the neighbor ACKs or is declared dead.
};
} // namespace routing::eigrp

#endif
