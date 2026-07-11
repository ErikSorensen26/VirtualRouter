/**
 * @file Neighbor.h
 * @brief OSPF neighbor: adjacency state machine, sequence tracking, and retransmission.
 */

/**
 * @defgroup OSPF_NEIGHBOR OSPF Neighbor
 * @ingroup OSPF
 * @brief Neighbor adjacency state machine, neighbor table, and retransmission tracking.
 */

#ifndef OSPF_NEIGHBOR_H
#define OSPF_NEIGHBOR_H

#include <atomic>
#include <optional>
#include <IPAddress.h>

#include "ospf/transmission/OspfPacket.hpp"
#include "ospf/database/LsaKey.hpp"
#include "ospf/neighbor/Retransmission.hpp"

namespace routing::ospf
{
struct LsaKey;
class Retransmission;
class OspfInterface;
class InterfaceTimers;

/**
 * @brief Represents a single OSPF adjacency peer on one interface.
 * @ingroup OSPF_NEIGHBOR
 *
 * `Neighbor` models the RFC 2328 §10 / RFC 5340 neighbor data structure. Each
 * instance tracks:
 * - **State machine** — the adjacency progresses through Down → Init →
 *   2-Way → ExStart → Exchange → Loading → Full.
 * - **DB exchange** — the master/slave role, current DBD sequence number,
 *   and the last DBD packet awaiting acknowledgment.
 * - **Retransmission** — owns a `Retransmission` object that holds the
 *   per-neighbor LSU and LSR retransmission lists.
 * - **DR/BDR election data** — the neighbor's declared DR and BDR at the
 *   time the last Hello was received.
 *
 * ## Architectural Role
 * `Neighbor` is the leaf object in the OSPF ownership tree:
 * `OspfProcess → Area → OspfInterface → NeighborTable → Neighbor`.
 * The rest of the protocol (flooding, SPF, LSA origination) interacts with
 * neighbors indirectly through `NeighborTable` and `OspfInterface`.
 *
 * ## Lifecycle & Ownership
 * Neighbors are created by `NeighborTable::createNeighbor` and destroyed by
 * `NeighborTable::deleteNeighbor`. A neighbor is non-copyable and non-movable
 * because `Retransmission` holds back-references to the owning process and
 * interface.
 *
 * ## Concurrency Model
 * `dr`, `bdr`, `lastAuthSeq`, `currentSeq`, `priority`, `inactivityTimerId`,
 * and `isTransit` are `std::atomic` because the Hello receive path (data-plane
 * thread) may update them concurrently with the protocol scheduler thread
 * reading them for election or flooding decisions.
 *
 * @warning `setState` must only be called from the OSPF process scheduler
 * thread. Driving state transitions from multiple threads without
 * synchronisation will corrupt the state machine.
 *
 * @see NeighborTable
 * @see Retransmission
 */
class Neighbor
{
public:
    /**
     * @brief RFC 2328 §10.1 neighbor state.
     *
     * Values are ordered so that higher numeric values represent more
     * advanced adjacency stages.
     */
    enum class State
    {
        DOWN     , ///< No Hello received; neighbor is inactive.
        ATTEMPT  , ///< Hello has been sent; used on NBMA networks before any response.
        INIT     , ///< Hello received but this router's RID not yet in neighbor's Hello.
        TWOWAY   , ///< Bidirectional communication established; DR/BDR election eligible.
        EXSTART  , ///< Master/slave negotiation in progress; first DBD exchange.
        EXCHANGE , ///< Exchanging database description packets.
        LOADING  , ///< Requesting LSAs missing from the local LSDB.
        FULL     , ///< Adjacency complete; databases are synchronized.
    };

    /**
     * @brief Master/slave role negotiated during the ExStart state.
     */
    enum class Role
    {
        SLAVE,  ///< This router is the slave; echoes the master's DBD sequence numbers.
        MASTER, ///< This router drives the DBD sequence numbers.
        NONE,   ///< Role not yet determined (before ExStart).
    };

    /**
     * @brief Constructs a new neighbor and initialises it to the Down state.
     *
     * Allocates the `Retransmission` object and arms the inactivity timer.
     *
     * @param iface      The interface on which this neighbor was discovered.
     * @param tmgr       The interface timer manager, used to start/cancel
     *                   the inactivity timer.
     * @param rid        The neighbor's OSPF Router ID.
     * @param neighborIp The neighbor's IP address (source of Hello packets).
     * @param unicast    True if this is a statically configured unicast neighbor.
     */
    explicit Neighbor(OspfInterface& iface, InterfaceTimers& tmgr,
                      uint32_t rid, types::IPAddress& neighborIp,
                      bool unicast = false);

    /**
     * @brief Destroys the neighbor.
     *
     * Cancels all in-flight retransmission timers and the inactivity timer
     * before releasing resources.
     */
    ~Neighbor();

    Neighbor(const Neighbor&) = delete;
    Neighbor& operator=(const Neighbor&) = delete;
    Neighbor(Neighbor&&) = delete;
    Neighbor& operator=(Neighbor&&) = delete;

    // STATE

    /**
     * @brief Returns the neighbor's current RFC 2328 §10.1 state-machine state.
     *
     * Progression runs Down → Init → 2-Way → ExStart → Exchange → Loading →
     * Full; routing information from this neighbor is only trusted once the
     * adjacency reaches Full.
     */
    State getState() const { return state; }

    /**
     * @brief Transitions the neighbor to a new state.
     *
     * Performs the actions mandated by the RFC for each state transition
     * (e.g. starting/stopping timers, clearing retransmission lists). Logs
     * the transition for debugging.
     *
     * @param s  The target state.
     * @return   True if the transition changed the state, false if @p s equals
     *           the current state.
     *
     * @warning Must only be called from the OSPF process scheduler thread.
     */
    bool setState(State s);

    // ROLE

    Role getRole() { return role; }
    void setRole(Role r) { role = r; }
    bool isMaster() { return getRole() == Role::MASTER; }

    /**
     * @brief Resets all DB exchange state after an ExStart restart.
     *
     * Clears the current DBD sequence number, the master/slave role, the
     * last DBD packet, and the pending LSR list so that a clean exchange
     * can begin.
     */
    void resetDbExchange();

    OspfInterface& getIface() const { return iface; }

    std::atomic<uint32_t> dr{0};  ///< DR router ID declared in the neighbor's last Hello.
    std::atomic<uint32_t> bdr{0}; ///< BDR router ID declared in the neighbor's last Hello.

    /**
     * @brief Returns true if this neighbor is claiming to be the DR on the segment.
     */
    bool isDr() { return dr.load(std::memory_order_relaxed) == routerID; }

    /**
     * @brief Returns true if this neighbor is claiming to be the BDR on the segment.
     */
    bool isBdr() { return bdr.load(std::memory_order_relaxed) == routerID; }

public:
    std::optional<LsaKey> currentDbd = std::nullopt; ///< Key of the DBD packet currently awaiting acknowledgment.

    const types::IPAddress ipAddress; ///< Neighbor's IP address; source address of Hello packets.
    const bool unicast{false};        ///< True for statically configured NBMA unicast neighbors.
    const uint32_t routerID;          ///< Neighbor's OSPF Router ID.
    const uint16_t mtu;               ///< MTU advertised by the neighbor in its DBD packets.
    uint32_t neighborInterfaceId = 0; ///< OSPFv3 interface ID reported by the neighbor (used in LSA cross-referencing).

    std::atomic<uint32_t> lastAuthSeq{0}; ///< Last authentication sequence number seen; guards against replay.
    std::atomic<uint32_t> currentSeq;    ///< Current DBD sequence number (master: own counter; slave: echo of master).
    std::atomic<uint8_t> priority;       ///< Neighbor's router priority as declared in its Hello.

    // TIMERS
    std::atomic<uint32_t> inactivityTimerId{0}; ///< Active dead-interval timer ID; 0 when not running.

    // RETRANSMISSION

    Retransmission& getRtr() { return rtr; }
    const Retransmission& getRtr() const { return rtr; }

    std::atomic<bool> isTransit{true}; ///< False once it is confirmed this neighbor need not receive flooded LSAs.

private:
    State state;
    Role role = Role::NONE;

    Retransmission rtr;       ///< Holds LSU and LSR retransmission lists for this neighbor.
    OspfInterface& iface;
    InterfaceTimers& tmgr;
};

} // namespace routing::ospf

#endif // OSPF_NEIGHBOR_H
