/**
 * @file InterfaceTimers.h
 * @brief Hello, hold, retransmission, and dampening timer management for one
 *        EIGRP interface.
 */

#ifndef EIGRP_INTERFACE_TIMERS_H
#define EIGRP_INTERFACE_TIMERS_H

#include <cstdint>
#include <chrono>
#include <atomic>
#include <IPAddress.h>

namespace core { class Global; }
namespace core { class ProcessQueue; }
namespace packet { struct StaticHeader; }

class Internal_EigrpTest;

namespace routing::eigrp
{
struct OutgoingQuery;
class EigrpInterface;
class Neighbor;
class ReliablePacket;
class Eigrp;
class MulticastReliablePacket;
class UnicastReliablePacket;
struct ReliableInfo;

/**
 * @brief Manages all timers associated with a single EIGRP interface.
 *
 * @c InterfaceTimers centralises scheduling and cancellation of:
 * - The periodic Hello timer (and the first Hello send).
 * - Per-neighbor hold timers (neighbor dead detection).
 * - Per-packet retransmission timers for reliable multicast and unicast.
 * - Per-neighbor graceful-restart timers.
 * - Dampening timers (reset, restart, and interval).
 *
 * All timer callbacks are dispatched through the @c core::ProcessQueue to
 * ensure serialised execution.
 *
 * @ingroup EIGRP_INTERFACE
 */
class InterfaceTimers
{
public:
    friend class Internal_EigrpTest;

    /**
     * @brief Constructs an InterfaceTimers for the given interface.
     * @param iface     The owning EigrpInterface.
     * @param scheduler The process-level scheduler used to post timer events.
     */
    InterfaceTimers(EigrpInterface& iface, core::ProcessQueue& scheduler);

    /**
     * @brief Cancels all outstanding timers and releases resources.
     */
    ~InterfaceTimers();

    // HELLO

    /**
     * @brief Starts the periodic Hello timer using the configured hello
     *        interval.
     */
    void startHello();

    /**
     * @brief Reschedules the next Hello transmission (called after each send).
     */
    void scheduleHello();

    /**
     * @brief Cancels the Hello timer; no further Hellos will be sent until
     *        @c startHello() is called again.
     */
    void stopHello();

    /**
     * @brief Immediately transmits a Hello packet on this interface.
     */
    void sendHello();

    // HOLD

    /**
     * @brief Starts or resets the hold timer for a neighbor.  If the timer
     *        expires before being cancelled, @c handleHoldTimeExpire is called.
     * @param neighbor The neighbor whose hold timer is being (re)started.
     */
    void startHoldTimer(Neighbor& neighbor);

    /**
     * @brief Cancels the hold timer for a neighbor (e.g., on Hello receipt).
     * @param neighbor The neighbor whose hold timer should be cancelled.
     */
    void cancelHoldTimer(Neighbor& neighbor);

    /**
     * @brief Invoked when a neighbor's hold timer expires; tears the neighbor
     *        down.
     * @param neighbor The neighbor that has expired.
     */
    void handleHoldTimeExpire(Neighbor& neighbor);

    // RETRANSMISSION

    /**
     * @brief Starts the retransmission timer for one neighbor's slot within a
     *        reliable multicast packet.
     * @param neighbor  The neighbor that has not yet ACKed (may be @c nullptr
     *                  for a new multicast).
     * @param multicast The multicast reliable packet being tracked.
     * @param info      Per-neighbor reliable delivery info to update.
     * @param seq       Sequence number of the packet.
     */
    void startRetransmissionTimer(Neighbor* neighbor, MulticastReliablePacket& multicast, ReliableInfo& info, uint32_t seq);

    /**
     * @brief Starts the retransmission timer for a unicast reliable packet.
     * @param neighbor The target neighbor.
     * @param unicast  The unicast reliable packet being tracked.
     * @param seq      Sequence number of the packet.
     */
    void startRetransmissionTimer(Neighbor* neighbor, UnicastReliablePacket& unicast, uint32_t seq);

    /**
     * @brief Cancels the retransmission timer associated with a reliable
     *        delivery info record.
     * @param pkt The reliable delivery info whose timer should be cancelled.
     */
    void cancelRetransmissionTimer(ReliableInfo& pkt);

    /**
     * @brief Cancels all timers (hold, graceful, retransmission) for a
     *        specific neighbor.
     * @param neighbor The neighbor being cleaned up.
     */
    void cancelNeighborTimers(Neighbor& neighbor);

    /**
     * @brief Starts the graceful-restart timer for a neighbor.
     * @param neighbor The neighbor entering graceful restart.
     */
    void startGracefulTimer(Neighbor& neighbor);

    /**
     * @brief Cancels the graceful-restart timer for a neighbor.
     * @param neighbor The neighbor whose graceful timer should be stopped.
     */
    void cancelGracefulTimer(Neighbor& neighbor);

    // DAMPENING

    /** @brief Restarts the dampening penalty decay (reset) timer. */
    void restartDampeningResetTimer();

    /** @brief Restarts the dampening suppression end (restart) timer. */
    void restartDampeningRestartTimer();

    /** @brief Starts the periodic dampening interval tick timer. */
    void startDampeningIntervalTimer();

    /**
     * @brief Returns @c true when the dampening suppression period has elapsed.
     */
    bool isDampenExpired() { return std::chrono::steady_clock::now() >= suppressedUntil; }

private:

    // HELLO TIMER
    std::atomic<uint32_t> helloTimerId = 0;                    ///< Timer ID for the Hello timer.
    std::chrono::steady_clock::time_point helloStartTime;      ///< Start time for the Hello timer.

    // DAMPENING TRACKING
    std::chrono::steady_clock::time_point suppressedUntil;     ///< Absolute time when suppression ends.
    std::atomic<uint32_t> dampeningResetId{0};                 ///< Timer ID for the dampening reset (decay) timer.
    std::atomic<uint32_t> dampeningRestartId{0};               ///< Timer ID for the dampening restart timer.
    std::atomic<uint32_t> dampeningIntervalId{0};              ///< Timer ID for the dampening interval tick.

    std::atomic<bool> runTimers = true; ///< Flag to indicate if timers should continue running.

    Eigrp* base;                      ///< Owning EIGRP process (used to post work items).
    EigrpInterface& iface;            ///< The interface these timers belong to.
    core::ProcessQueue& scheduler;    ///< Scheduler used to register and cancel timers.
};
} // namespace routing::eigrp

#endif // EIGRP_TIMER_MANAGER_H

