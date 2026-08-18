/**
 * @file ReliableTransport.h
 * @brief EIGRP RTP (Reliable Transport Protocol): retransmission and acknowledgment.
 */

/**
 * @defgroup EIGRP_RTP EIGRP RTP
 * @ingroup EIGRP
 * @brief Reliable Transport Protocol: retransmission, neighbors, packet builder, TLV builder.
 */

#ifndef EIGRP_RELIABLE_TRANSPORT_H
#define EIGRP_RELIABLE_TRANSPORT_H

#include <unordered_map>
#include <IPAddress.h>
#include <atomic>

#include "packet/TlvOptions.hpp"
#include "eigrp/rtp/Neighbor.h"

/// Maximum number of retransmission attempts before declaring a neighbor dead.
#define MAX_RETRANSMISSIONS 16

namespace processing { class PacketBuilder; }
class Internal_EigrpTest;

namespace routing::eigrp
{
class NeighborTable;

struct OutgoingQuery;
struct ActiveRoute;
struct RouteInfo;

/**
 * @brief Implements EIGRP's Reliable Transport Protocol (RTP) for one interface.
 * @ingroup EIGRP_RTP
 *
 * `ReliableTransport` is the per-interface send/receive engine for all EIGRP
 * packet types. It enforces the RTP contract: packets that require
 * acknowledgment (UPDATE, QUERY, REPLY, SIA-QUERY, SIA-REPLY) are retransmitted
 * until ACKed or the neighbor is declared down. HELLO and ACK packets are sent
 * unreliably.
 *
 * Each instance owns:
 * - A sequence number counter shared across all reliable sends on the interface.
 * - The in-flight multicast reliable packet map (`reliablePackets`).
 * - Logic for both the multicast-first / unicast-retransmit flow and for direct
 *   unicast reliable sends.
 *
 * ## Architectural Role
 * One `ReliableTransport` is owned by each `EigrpInterface`. It is the sole
 * point of contact between the EIGRP protocol logic (@ref RouteManager,
 * @ref DualEngine) and the hardware packet path. Higher-level code calls the
 * `send*` family of methods; lower-level formatting is delegated to
 * @ref EigrpPacketBuilder and @ref TLVBuilder.
 *
 * ## Lifecycle & Ownership
 * Constructed with a reference to the owning @ref EigrpInterface. The interface
 * must outlive `ReliableTransport`. Destruction cancels no timers directly;
 * callers must drain the interface's timer queue before destroying this object.
 *
 * ## Concurrency Model
 * All public methods must be called from the EIGRP interface scheduler thread.
 * `pendingPeerTermination` is the one exception: it is an `atomic<bool>` and
 * may be set from any thread to signal that the interface should tear down its
 * adjacencies at the next scheduling opportunity.
 * `nextSeq` is also atomic to allow safe reads from monitoring threads, though
 * increments happen only on the scheduler thread.
 *
 * @warning Calling `send*` methods from outside the scheduler thread will race
 * with the retransmission timer callbacks and corrupt the `reliablePackets` map.
 *
 * @see EigrpInterface
 * @see NeighborTable
 * @see ReliablePacket.hpp
 */
class ReliableTransport
{
public:
    friend class ::Internal_EigrpTest;

    /**
     * @brief Aggregates the received EIGRP packet header, source IP, parsed TLV
     *        options, and resolved neighbor pointer for a single incoming packet.
     * @ingroup EIGRP_RTP
     *
     * Constructed on the stack in @ref handleIncoming and passed by reference
     * into each `process*` handler so they share a single parsed context without
     * re-parsing.
     */
    struct RTPInfo
    {
        /**
         * @brief Constructs from the wire-decoded header and the IP source address.
         *
         * @param eigrp      Reference to the parsed EIGRP common header.
         * @param neighborIp Source IP of the packet; used to look up the neighbor.
         */
        RTPInfo(const packet::EigrpHeader& eigrp, const types::IPAddress& neighborIp) : eigrp(eigrp), neighborIp(neighborIp) {}
        const packet::EigrpHeader& eigrp;
        const types::IPAddress& neighborIp;
        std::vector<packet::TLV16Option> opts = {}; ///< TLV options decoded from the packet payload.
        Neighbor* neighbor = nullptr;               ///< Resolved neighbor, or nullptr if not yet established.
    };

    /**
     * @brief Constructs the transport layer for the given EIGRP interface.
     *
     * Initializes the sequence counter to 1 (sequence 0 is reserved),
     * resolves the address family and AS number from the interface, and
     * obtains a pointer to the interface's @ref NeighborTable.
     *
     * @param iface Owning EIGRP interface; must outlive this object.
     */
    ReliableTransport(EigrpInterface& iface);

    /**
     * @brief Destructs the transport layer.
     *
     * Does not cancel outstanding retransmission timers. The caller must
     * ensure all timers sourced from this interface are cancelled before
     * destroying the object to prevent use-after-free in timer callbacks.
     */
    ~ReliableTransport();

    /**
     * @brief Controls the topology synchronization mode for a full-table send.
     *
     * Passed to @ref sendFullTopology to indicate whether the outgoing UPDATE
     * stream is a fresh topology dump, an INIT sequence start, or the REPLY
     * that concludes an INIT exchange.
     */
    enum class Resync { NONE, INIT, REPLY };

    /**
     * @brief Entry point for all received EIGRP packets on this interface.
     *
     * Dispatches to the appropriate `process*` handler based on the opcode in
     * `eigrpPacket`. Validates the AS number and sequence number before
     * forwarding to opcode-specific logic.
     *
     * @param ipStart      Pointer to the start of the IP header (used for
     *                     checksum verification).
     * @param eigrpPacket  Decoded EIGRP common header from the received packet.
     * @param neighborIp   Source IP address of the packet.
     * @param multicast    True if the packet arrived on the multicast address.
     */
    void handleIncoming(const uint8_t* ipStart, const packet::EigrpHeader& eigrpPacket, const types::IPAddress& neighborIp, bool multicast);

    /// Set from any thread to request graceful teardown of all adjacencies.
    std::atomic<bool> pendingPeerTermination{false};

    /**
     * @brief Sends a multicast HELLO to all neighbors on the interface.
     *
     * HELLOs are sent unreliably; no ACK is expected. They carry K-values,
     * hold time, and optionally stub and authentication TLVs.
     */
    void sendHello();

    /**
     * @brief Sends a unicast HELLO directly to a specific neighbor IP.
     *
     * Used during neighbor formation when multicast is suppressed, or to
     * force an immediate adjacency check on a specific peer.
     *
     * @param neighborIp Destination IP of the target neighbor.
     */
    void sendUnicastHello(const types::IPAddress& neighborIp);

    /**
     * @brief Sends a conditional-receive HELLO to suppress multicast delivery.
     *
     * A conditional-receive HELLO includes a sequence TLV listing all neighbors
     * that must have received the preceding multicast reliable packet before
     * the next multicast is sent. This prevents fast neighbors from receiving
     * packets that slow neighbors have not yet ACKed.
     *
     * @param neighbors List of neighbor IPs to include in the sequence TLV.
     * @param seq       Sequence number of the multicast reliable packet that
     *                  these neighbors must acknowledge first.
     */
    void sendConditionalHello(const std::vector<types::IPAddress>& neighbors, uint32_t seq);

    /**
     * @brief Sends a standalone ACK to the given neighbor for `seqNum`.
     *
     * ACKs are unreliable and carry no payload. They are sent as unicast
     * HELLO packets with the acknowledgment field set.
     *
     * @param neighbor Target neighbor.
     * @param seqNum   Sequence number being acknowledged.
     */
    void sendAck(Neighbor& neighbor, uint32_t seqNum);

    /**
     * @brief Records a pending ACK that must be sent to the neighbor.
     *
     * Deferred ACKs are coalesced into the next outgoing reliable packet
     * piggybacked in the acknowledgment field, reducing ACK-only traffic.
     *
     * @param neighbor Target neighbor.
     * @param seq      Sequence number to acknowledge.
     */
    void trackAck(Neighbor& neighbor, uint32_t seq);

    /**
     * @brief Sends any pending ACK for the neighbor if the queue is non-empty.
     *
     * Called after processing a reliable packet to flush any ACK that could
     * not be piggybacked onto an outgoing reliable packet.
     *
     * @param neighbor Target neighbor.
     * @param seq      Sequence number to acknowledge if no piggyback opportunity arose.
     */
    void attemptSendAck(Neighbor& neighbor, uint32_t seq);

    /**
     * @brief Sends an empty UPDATE with the End-of-Table flag to signal topology sync completion.
     *
     * A null UPDATE (no route TLVs) with the EoT bit set tells the neighbor
     * that the full topology dump has finished and normal incremental updates
     * will follow.
     *
     * @param neighbor Target neighbor to receive the null UPDATE.
     */
    void sendNullUpdate(Neighbor& neighbor);

    /**
     * @brief Sends the complete local topology table to a neighbor.
     *
     * Used during initial adjacency formation or after a resynchronization
     * event. The `resync` parameter controls whether INIT or REPLY flags are
     * set on the outgoing UPDATE stream.
     *
     * @param neighbor Target neighbor.
     * @param resync   Sync mode: NONE for a normal full send, INIT to start
     *                 an INIT exchange, REPLY to conclude one.
     */
    void sendFullTopology(Neighbor& neighbor, Resync resync = Resync::NONE);

    /**
     * @brief Sends an incremental UPDATE for the specified routes.
     *
     * Routes with changed metrics or reachability are advertised. If `neighbor`
     * is null the UPDATE is sent to all neighbors via multicast; otherwise it
     * is unicast to the specified peer.
     *
     * @param neighbor Target neighbor for a unicast UPDATE, or nullptr for multicast.
     * @param routes   Routes to advertise in this UPDATE.
     */
    void sendUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);

    /**
     * @brief Sends a poisoned UPDATE advertising unreachable metrics for the given routes.
     *
     * Poison reverse causes routes to be advertised back with an infinite
     * metric to break potential count-to-infinity scenarios during DUAL
     * convergence.
     *
     * @param neighbor Target neighbor, or nullptr to multicast to all peers.
     * @param routes   Routes to poison.
     */
    void sendPoisenedUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);

    /**
     * @brief Sends a QUERY for the given active routes to all neighbors.
     *
     * DUAL initiates a query when a route goes active (no feasible successor
     * exists). Queries are sent reliably to all neighbors and require a REPLY
     * from each before the route can return to passive.
     *
     * @param routes Active routes for which replies are being solicited.
     */
    void sendQuery(const std::vector<ActiveRoute*>& routes);

    /**
     * @brief Sends a unicast QUERY to a specific neighbor.
     *
     * Used when only a subset of neighbors need to be queried (e.g., stub
     * filtering reduces the query scope).
     *
     * @param neighbor Target neighbor.
     * @param routes   Queries to send.
     */
    void sendUnicastQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes);

    /**
     * @brief Sends REPLY packets to a neighbor for the given routes.
     *
     * A REPLY is the response to a received QUERY and conveys this router's
     * current best metric for each queried destination.
     *
     * @param neighbor Neighbor that originated the QUERY.
     * @param routes   Routes for which replies are being sent.
     */
    void sendReply(Neighbor& neighbor, const std::vector<const RouteInfo*>& routes);

    /**
     * @brief Sends a Stuck-In-Active (SIA) QUERY to a specific neighbor.
     *
     * Sent when a QUERY has been outstanding for more than half the active
     * timer interval. If the neighbor does not reply with a SIA-REPLY, it
     * is declared stuck and the adjacency is reset.
     *
     * @param neighbor Target neighbor.
     * @param routes   Queries that are approaching the SIA threshold.
     */
    void sendSIAQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes);

    /**
     * @brief Sends a SIA-REPLY to confirm this router is still computing the route.
     *
     * A SIA-REPLY tells the querying neighbor that this router is still alive
     * and actively running DUAL, preventing a spurious adjacency reset.
     *
     * @param neighbor Neighbor that sent the SIA-QUERY.
     */
    void sendSIAReply(Neighbor& neighbor);

    /**
     * @brief Validates and tracks the sequence number of an incoming reliable packet.
     *
     * Checks for duplicates and out-of-order delivery. Updates
     * `neighbor->lastSeqRecv` on acceptance. Returns false if the packet
     * should be silently dropped.
     *
     * @param info Parsed packet context including the resolved neighbor pointer.
     * @param seq  Sequence number from the EIGRP header.
     * @return True if the packet is in order and should be processed; false to discard.
     */
    bool validateSeqNum(RTPInfo& info, uint32_t seq);

    /**
     * @brief Prepares the EIGRP header for a unicast reliable transmission.
     *
     * Sets the sequence number and records the packet in the neighbor's
     * reliable queue so retransmissions can be scheduled if no ACK arrives.
     *
     * @param neighbor Target neighbor.
     * @param info     EIGRP header to populate with sequence and ACK fields.
     * @return True if the header was set up successfully.
     */
    bool setupUnicastReliable(Neighbor& neighbor, packet::EigrpHeader& info);

    /**
     * @brief Prepares the EIGRP header for a multicast reliable transmission.
     *
     * Assigns a sequence number and sets flags required for the RTP
     * conditional-receive mechanism.
     *
     * @param info EIGRP header to populate.
     * @return True if the header was set up successfully.
     */
    bool setupMulticastReliable(packet::EigrpHeader& info);

    /**
     * @brief Registers a multicast reliable packet and arms its retransmission timers.
     *
     * Called immediately after the multicast is transmitted. Creates a
     * @ref MulticastReliablePacket entry in `reliablePackets` and schedules
     * per-neighbor retransmission timers using the RTO derived from each
     * neighbor's SRTT.
     *
     * @param pkt The just-sent multicast packet snapshot.
     * @param seq Sequence number assigned to this packet.
     */
    void startMulticastReliable(MulticastReliablePacket& pkt, uint32_t seq);

    /**
     * @brief Registers a unicast reliable packet and arms its retransmission timer.
     *
     * Called immediately after the unicast is transmitted. Stores the packet
     * in the neighbor's `reliablePackets` map and schedules a retransmission
     * timer based on the neighbor's current RTO.
     *
     * @param nbr Target neighbor.
     * @param pkt The just-sent unicast packet snapshot.
     * @param seq Sequence number assigned to this packet.
     */
    void startUnicastReliable(Neighbor& nbr, UnicastReliablePacket& pkt, uint32_t seq);

    /**
     * @brief Retransmits a reliable packet directly to a neighbor.
     *
     * Bypasses the normal send path and writes the frozen packet bytes
     * directly to the hardware output queue.
     *
     * @param neighbor Target neighbor.
     * @param header   Frozen packet bytes to retransmit.
     */
    void sendRetransmission(Neighbor& neighbor, packet::StaticHeader& header);

    /**
     * @brief Handles a retransmission timeout for a multicast reliable packet.
     *
     * Checks whether `neighbor` has exceeded @ref MAX_RETRANSMISSIONS. If so,
     * tears down the adjacency. Otherwise retransmits the packet unicast and
     * reschedules the timer with exponential back-off.
     *
     * @param neighbor Neighbor that has not yet ACKed; nullptr if called during cleanup.
     * @param pkt      The multicast reliable packet awaiting acknowledgment.
     * @param info     Per-neighbor retransmission state within `pkt`.
     * @param seq      Sequence number of the packet being retransmitted.
     */
    void handleRetransmission(Neighbor* neighbor, MulticastReliablePacket& pkt, ReliableInfo& info, uint32_t seq);

    /**
     * @brief Handles a retransmission timeout for a unicast reliable packet.
     *
     * Checks the neighbor's retransmission count. Tears down the adjacency at
     * @ref MAX_RETRANSMISSIONS; otherwise retransmits and reschedules.
     *
     * @param neighbor Neighbor that has not yet ACKed; nullptr if called during cleanup.
     * @param pkt      The unicast reliable packet to retransmit.
     * @param seq      Sequence number of the packet being retransmitted.
     */
    void handleRetransmission(Neighbor* neighbor, UnicastReliablePacket& pkt, uint32_t seq);

    /**
     * @brief Atomically increments and returns the interface sequence counter.
     *
     * Sequence numbers wrap naturally via unsigned overflow. Sequence 0 is
     * never assigned (counter starts at 1).
     *
     * @return The newly assigned sequence number.
     */
    uint32_t incrementSequenceNumber();

    /// Returns the current (next-to-use) sequence number without incrementing.
    uint32_t getSeq() { return nextSeq.load(std::memory_order_relaxed); }

    // MULTICAST RELIABLE

    /// In-flight multicast reliable packets keyed by sequence number.
    std::unordered_map<uint32_t, MulticastReliablePacket> reliablePackets;

    /**
     * @brief Per-packet metric and format metadata computed once at send time.
     * @ingroup EIGRP_RTP
     *
     * Gathered before packet construction and passed through the `create*`
     * helpers so metric-dependent fields are written only once rather than
     * recomputed per TLV.
     */
    struct PktInfo
    {
        uint64_t bandwidthMetric{0}; ///< Composite bandwidth metric for this send.
        uint64_t delay{0};           ///< Composite delay metric for this send.
        TLVType version;             ///< Classic or wide TLV format for route encoding.
        uint16_t mtu;                ///< Interface MTU; limits route TLVs per packet.
        size_t sent{0};              ///< Number of route TLVs written so far (used for fragmentation).
    };

private:

    void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest = nullptr);
    void transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::EigrpHeader& header);
    void releaseFailedPacket(processing::PacketBuilder& builder);
    void createPacket(processing::PacketBuilder& builder);

    std::optional<packet::EigrpHeader> createHello(processing::PacketBuilder& builder);
    std::optional<packet::EigrpHeader> createUnicastHello(processing::PacketBuilder& builder);
    std::optional<packet::EigrpHeader> createConditionalHello(processing::PacketBuilder& builder, PktInfo& info, const std::vector<types::IPAddress>& neighbors, uint32_t seq);
    std::optional<packet::EigrpHeader> createAck(processing::PacketBuilder& builder, uint32_t seq);
    std::optional<packet::EigrpHeader> createNullUpdate(processing::PacketBuilder& builder);
    std::optional<packet::EigrpHeader> createUpdate(processing::PacketBuilder& builder, PktInfo& info, Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    std::optional<packet::EigrpHeader> createQuery(processing::PacketBuilder& builder, PktInfo& info, const std::vector<ActiveRoute*>& queries);
    std::optional<packet::EigrpHeader> createUnicastQuery(processing::PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries);
    std::optional<packet::EigrpHeader> createReply(processing::PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<const RouteInfo*>& replies);
    std::optional<packet::EigrpHeader> createSIAQuery(processing::PacketBuilder& builder, PktInfo& info, const std::vector<OutgoingQuery*>& queries);
    std::optional<packet::EigrpHeader> createSIAReply(processing::PacketBuilder& builder);

    bool processConditionalReceive(uint32_t seq, Neighbor& neighbor);

    void processAck(Neighbor& neighbor, const uint32_t seq);
    void processHello(RTPInfo& info, bool unicast);
    void processUpdate(RTPInfo& info);
    void processQuery(RTPInfo& info);
    void processSIAQuery(RTPInfo& info);
    void processReply(RTPInfo& info);
    void processSIAReply(RTPInfo& info);

    void checkInit(Neighbor& neighbor);

    bool verifyNeighborAS(const packet::EigrpHeader& header);
    uint16_t getMtu();

    std::atomic<uint32_t> nextSeq = 1; ///< Next sequence number for outgoing reliable packets; never 0.
    std::atomic<bool> firstFullSend = false; ///< True once the first full topology dump has been sent on this interface.

    types::AddressFamily af; ///< Address family (IPv4/IPv6) served by this interface.
    uint32_t as;             ///< EIGRP AS number; used to validate incoming packets.

    NeighborTable* ntable = nullptr; ///< Peer adjacency table for this interface.
    EigrpInterface& iface;
};
} // namespace routing::eigrp

#endif // EIGRP_RELIABLE_TRANSPORT_H
