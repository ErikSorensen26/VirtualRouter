/**
 * @file Neighbor.h
 * @brief EIGRP neighbor state: sequencing, retransmission, and adjacency control.
 */

#ifndef EIGRP_NEIGHBOR_H
#define EIGRP_NEIGHBOR_H

#include <map>
#include <IPAddress.h>
#include <atomic>
#include <deque>
#include <set>

#include "ReliablePacket.hpp"
#include "packet/headers/EigrpHeader.hpp"

namespace routing::eigrp
{
class InterfaceTimers;
struct EigrpHeaderInfo;
class EigrpInterface;

/**
 * @brief TLV type family codes used to select the metric encoding format.
 * @ingroup EIGRP_RTP
 *
 * The high byte of a route TLV type determines the address family and metric
 * generation. `TLVType` identifies which family is in use so senders can
 * pick the correct route TLV layout without inspecting individual TLV codes.
 */
enum class TLVType : uint16_t
{
    LEGACY_V4 = 0x0100, ///< Classic IPv4 metric encoding (K1–K5, 32-bit fields).
    LEGACY_V6 = 0x0400, ///< Classic IPv6 metric encoding.
    WIDE     = 0x0600,  ///< Wide (named-mode) metric encoding with 64-bit delay.
};

/**
 * @brief Tracks per-peer EIGRP adjacency and RTP retransmission state.
 * @ingroup EIGRP_RTP
 *
 * A `Neighbor` object is the runtime state for one active EIGRP peer. It
 * combines adjacency lifecycle management (hold timers, INIT handshake,
 * graceful restart) with the RTP bookkeeping needed to drive reliable delivery
 * (sequence numbers, ACK queues, per-peer unicast reliable packets, RTT
 * estimates).
 *
 * Each instance contains:
 * - Atomic state and flag fields that reflect the current adjacency phase.
 * - A reliable packet map (`reliablePackets`) for in-flight unicast sends.
 * - ACK queues used to coalesce acknowledgments into outgoing packets.
 * - RTT estimates (SRTT / RTTVAR / RTO) maintained by @ref ReliableTransport.
 *
 * ## Architectural Role
 * `Neighbor` is the leaf-level data node in the RTP subsystem. It does not
 * drive retransmissions itself; @ref ReliableTransport reads and writes the
 * public fields and manages timers. @ref NeighborTable creates and destroys
 * `Neighbor` objects and routes down-events to the DUAL layer.
 *
 * ## Lifecycle & Ownership
 * Constructed by @ref NeighborTable::createNeighbor. Not copyable or movable
 * because in-place construction in a `std::map` is required and outstanding
 * timer callbacks hold raw pointers. Destroyed only by @ref NeighborTable when
 * the adjacency is explicitly torn down after all timers have been cancelled.
 *
 * ## Concurrency Model
 * All public fields use `std::atomic` where concurrent access is possible.
 * The `reliablePackets` map, `reliableQueue`, `activeConditions`, and
 * `receivedConditions` are **not** protected by a lock and must only be
 * accessed from the EIGRP interface scheduler thread.
 *
 * @warning Deleting a `Neighbor` while a retransmission timer callback holds a
 * pointer to it causes undefined behavior. @ref NeighborTable::cancelAllHoldTimers
 * must be called and all timer callbacks must have completed before destruction.
 *
 * @see NeighborTable
 * @see ReliableTransport
 */
class Neighbor
{
public:
    /**
     * @brief Operational adjacency state of the neighbor.
     */
    enum class State
    {
        DOWN,    ///< Adjacency is not established; no routes are accepted.
        PENDING, ///< HELLO received; INIT exchange in progress.
        UP,      ///< Full adjacency; route updates are processed normally.
    };

    /**
     * @brief Negotiated TLV capability version, derived from the HELLO exchange.
     *
     * The enum value matches the high-byte prefix of the corresponding TLV type
     * codes so it can be used directly to select encoding paths.
     */
    enum class Version : uint16_t
    {
        LEGACY = 0x0102, ///< Peer supports only classic (legacy) metric TLVs.
        WIDE   = 0x0200, ///< Peer supports wide (named-mode) metric TLVs.
        UNKNOWN = 0x0000, ///< Version not yet determined; HELLO not fully parsed.
    };

    /**
     * @brief Constructs the neighbor with its immutable identity fields.
     *
     * Establishes the IP address, TLV version (which determines `tlvType`),
     * and unicast flag. The adjacency state is initialized to `DOWN`; it
     * advances to `PENDING` once the INIT exchange starts.
     *
     * @param iface      Owning EIGRP interface.
     * @param tmgr       Interface timer manager used for hold and graceful-restart timers.
     * @param neighborIp IP address of the peer; immutable after construction.
     * @param version    TLV capability level negotiated from the first HELLO.
     * @param unicast    True if this peer was manually configured for unicast-only delivery.
     */
    explicit Neighbor(EigrpInterface& iface, InterfaceTimers& tmgr, const types::IPAddress& neighborIp, Version version, bool unicast = false);

    /**
     * @brief Destroys the neighbor and cancels its hold timer.
     *
     * The hold timer is cancelled here as a safety measure, but callers should
     * cancel it explicitly before destruction to avoid callbacks firing on a
     * partially destroyed object.
     */
    ~Neighbor();

    Neighbor(const Neighbor&) = delete;
    Neighbor& operator=(const Neighbor&) = delete;
    Neighbor(Neighbor&&) = delete;
    Neighbor& operator=(Neighbor&&) = delete;

    // STATE

    State getState() const noexcept { return state.load(std::memory_order_relaxed); }
    void setState(State newState) { state.store(newState, std::memory_order_release); }

    // RELIABLE PACKET MANAGEMENT

    /**
     * @brief Cancels and removes all in-flight unicast reliable packets for this neighbor.
     *
     * Called during adjacency teardown to discard pending retransmissions before
     * the neighbor object is deleted. Outstanding retransmission timers must be
     * cancelled by the caller before invoking this.
     */
    void clearReliable();

    // CONTROL

    /**
     * @brief Resets all adjacency state in preparation for a new HELLO exchange.
     *
     * Clears sequence numbers, INIT flags, RTT estimates, and the ACK queues.
     * Called when a neighbor is declared down but its entry is being recycled
     * (e.g., a new HELLO arrives from the same IP before the entry is removed).
     */
    void clear();

    /**
     * @brief Returns true if the neighbor has completed INIT and is in the UP state.
     *
     * Convenience predicate used by @ref ReliableTransport to gate route
     * advertisement: routes are not sent to a neighbor that has not yet
     * finished the INIT handshake.
     */
    bool isActive() const noexcept;

    EigrpInterface& getIface() const { return iface; }

public:

    // PUBLIC FIELDS

    const types::IPAddress ipAddress;
    const bool unicast{false};              ///< True if this peer bypasses multicast delivery entirely.
    std::atomic<bool> fullSent = false;     ///< True once the initial full topology dump has been sent to this neighbor.
    std::atomic<bool> initComplete{false};  ///< True after the INIT exchange (both INIT UPDATE and null UPDATE) has completed.
    std::atomic<bool> initInProgress{false};///< True while the local side is sending the INIT UPDATE stream.
    std::atomic<bool> resyncInProgress{false}; ///< True during a triggered resynchronization.
    std::atomic<bool> isStub{false};        ///< True if the neighbor advertised stub capability in its HELLO.
    std::atomic<uint32_t> lastSeqRecv = 0; ///< Highest sequence number received from this neighbor; used for duplicate detection.
    std::atomic<uint32_t> lastSeqAck = 0;  ///< Highest sequence number this neighbor has acknowledged from us.

    Version version;       ///< Negotiated TLV capability; governs metric encoding for this peer.
    const TLVType tlvType; ///< Derived from `version` at construction; used directly for TLV selection.
    uint32_t routerID;     ///< Router ID advertised by this neighbor in its HELLO.

    /**
     * @brief Enqueues an ACK to be sent to this neighbor.
     *
     * ACKs are coalesced: the next outgoing reliable packet to this neighbor
     * will piggyback the ACK field rather than requiring a standalone ACK
     * packet. Returns false if the queue is at capacity.
     *
     * @param ack Sequence number to acknowledge.
     * @return True if the ACK was enqueued; false if dropped.
     */
    bool pushAck(uint32_t ack);

    /**
     * @brief Dequeues the oldest pending ACK.
     *
     * @param[out] ack Populated with the dequeued sequence number on success.
     * @return True if an ACK was available; false if the queue is empty.
     */
    bool popAck(uint32_t& ack);

    /**
     * @brief Removes a specific ACK from the pending queue without consuming it.
     *
     * Used when a retransmission subsumes an ACK that was already queued.
     *
     * @param ack Sequence number to remove.
     */
    void removeAck(uint32_t ack);

    /**
     * @brief Returns true if the given ACK is still in the pending queue.
     *
     * @param ack Sequence number to test.
     */
    bool hasAck(uint32_t ack);

    // TIMERS

    std::atomic<uint32_t> holdTimerId{0};     ///< Timer ID for the hold timer; 0 means no timer armed.
    std::atomic<uint32_t> gracefulTimerId{0}; ///< Timer ID for the graceful-restart window; 0 if not in graceful restart.
    std::atomic<uint16_t> holdTime{0};        ///< Hold time (seconds) advertised by this neighbor; refreshed on each HELLO.
    std::atomic<bool> isGraceful{false};      ///< True while this neighbor is in the graceful-restart window.
    std::atomic<bool> secondHello{false};     ///< True after the second HELLO is received; guards against premature adjacency formation.

    std::atomic<uint32_t> recvInitSeq{0}; ///< Sequence number of the INIT UPDATE received from this neighbor.
    std::atomic<uint32_t> sentInitSeq{0}; ///< Sequence number of the INIT UPDATE sent to this neighbor.

    /// Smoothed round-trip time estimate (seconds) used to compute the retransmission timeout.
    std::atomic<double> srtt{1.0};
    /// Round-trip time variance estimate; contributes to RTO calculation.
    std::atomic<double> rttvar{0.5};
    /// Retransmission timeout (seconds) derived from SRTT and RTTVAR.
    std::atomic<double> rto{1.5};

    std::atomic<uint32_t> currentReliable{0}; ///< Sequence number of the reliable packet currently pending ACK from this neighbor.
    std::deque<std::pair<uint32_t, bool>> reliableQueue; ///< Ordered queue of (seq, isUnicast) pairs waiting to be sent reliably.
    std::map<uint32_t, UnicastReliablePacket> reliablePackets; ///< In-flight unicast reliable packets keyed by sequence number.
    std::set<uint32_t> activeConditions; ///< Sequence numbers for which this neighbor is listed in a conditional-receive HELLO.
    std::unordered_map<uint32_t, bool> receivedConditions; ///< Tracks which conditional-receive sequence numbers this neighbor has responded to.

private:
    std::atomic<State> state{State::DOWN};

    std::deque<uint32_t> ackQueue;         ///< Pending ACKs to be piggybacked; bounded to avoid unbounded growth.
    std::set<uint32_t> outstandingAcks;    ///< Tracks ACKs already in `ackQueue` to prevent duplicates.

    EigrpInterface& iface;
    InterfaceTimers& tmgr;
};
} // namespace routing::eigrp

#endif // EIGRP_NEIGHBOR_H
