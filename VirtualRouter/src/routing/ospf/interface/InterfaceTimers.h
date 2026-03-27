/**
 * @file InterfaceTimers.h
 * @brief OSPF interface timer management: Hello, Dead, retransmit, and pacing.
 */

#ifndef OSPF_INTERFACE_TIMERS_H
#define OSPF_INTERFACE_TIMERS_H

#include <atomic>
#include <chrono>
#include <ControlScheduler.h>

#include "ospf/transmission/OspfPacket.hpp"

namespace core { class ProcessQueue; }

namespace routing::ospf
{
class Neighbor;
class OspfInterface;

/**
 * @brief Manages all periodic and one-shot timers associated with an OSPF interface.
 * @ingroup OSPF_INTERFACE
 *
 * Each `OspfInterface` owns exactly one `InterfaceTimers` instance. The class
 * centralises timer scheduling so that timer IDs, start times, and the
 * `ProcessQueue` reference are not scattered across the interface and neighbor
 * objects.
 *
 * Timers controlled here include:
 * - **Hello** — periodic transmission of Hello packets to maintain adjacencies.
 * - **Inactivity (Dead)** — per-neighbor timer that fires when no Hello has
 *   been received within the dead interval; triggers a neighbor state reset.
 * - **DBD retransmission** — resends an unacknowledged Database Description
 *   packet to a specific neighbor.
 * - **LSR retransmission** — resends unacknowledged Link-State Requests.
 * - **LSU retransmission** — resends unacknowledged Link-State Updates.
 * - **LSR / LSU pacing** — rate-limits flooding bursts to avoid overwhelming
 *   a neighbor on first contact or after a topology change.
 *
 * ## Architectural Role
 * `InterfaceTimers` is the single point of contact between the OSPF protocol
 * logic and the `core::ProcessQueue` scheduler. All OSPF timer callbacks are
 * posted through this class, keeping the rest of the interface and neighbor
 * code free of direct scheduler calls.
 *
 * ## Lifecycle & Ownership
 * Constructed by `OspfInterface` and destroyed with it. The `runTimers` flag
 * is set to `false` during teardown so that in-flight callbacks find the
 * guard and exit without touching freed state.
 *
 * ## Concurrency Model
 * `helloTimerId` and `runTimers` are `std::atomic` because the Hello timer
 * callback runs on the scheduler thread while the interface can be stopped
 * from a control-plane thread. All other timer IDs belong to neighbor objects
 * and are similarly atomic.
 *
 * @see OspfInterface
 * @see Neighbor
 */
class InterfaceTimers
{
public:
    /**
     * @brief Constructs the timer manager for the given interface.
     *
     * Initialises the scheduler reference from the interface's process queue.
     * No timers are started at construction time.
     *
     * @param iface  The owning OSPF interface.
     */
    explicit InterfaceTimers(OspfInterface& iface);

    /**
     * @brief Arms the Hello timer for a single future firing without starting
     *        the periodic Hello loop.
     *
     * Used during interface bring-up to send an initial Hello after a short
     * propagation delay before the regular interval begins.
     */
    void scheduleHello();

    /**
     * @brief Starts the periodic Hello timer using the configured Hello interval.
     *
     * If a Hello timer is already running it is replaced. The timer
     * automatically reschedules itself on each expiry as long as `runTimers`
     * is true.
     */
    void startHello();

    /**
     * @brief Stops the periodic Hello timer and cancels any pending Hello.
     */
    void stopHello();

    /**
     * @brief Immediately constructs and transmits a Hello packet on the interface.
     *
     * Called both by the Hello timer callback and on-demand when interface
     * parameters change (e.g. DR election result).
     */
    void sendHello();

    /**
     * @brief Starts the inactivity (Dead) timer for a neighbor.
     *
     * The timer fires after the configured dead interval. If not refreshed by
     * an incoming Hello the expiry handler transitions the neighbor to Down.
     *
     * @param neighbor  The neighbor whose inactivity timer is being armed.
     */
    void startInactiveTimer(Neighbor& neighbor);

    /**
     * @brief Cancels an in-progress inactivity timer for a neighbor.
     *
     * Called whenever a Hello is received from the neighbor so the dead
     * interval restarts from zero.
     *
     * @param neighbor  The neighbor whose timer should be cancelled.
     */
    void cancleInactiveTimer(Neighbor& neighbor);

    /**
     * @brief Handles expiry of a neighbor's inactivity timer.
     *
     * Drives the neighbor state machine to Down and initiates adjacency
     * teardown when no Hello has been received within the dead interval.
     *
     * @param neighbor  The neighbor whose dead interval has elapsed.
     */
    void handleInactiveTimeExpire(Neighbor& neighbor);

    /**
     * @brief Starts the DBD retransmission timer for a neighbor.
     *
     * Schedules a resend of the last Database Description packet if no
     * acknowledgment is received within the retransmit interval.
     *
     * @param neighbor  The neighbor awaiting DBD acknowledgment.
     */
    void startDbdRetransmissionTimer(Neighbor& neighbor);

    /**
     * @brief Starts the LSR retransmission timer for a neighbor.
     *
     * Fires if a Link-State Request is not answered by the corresponding LSU
     * within the retransmit interval.
     *
     * @param neighbor  The neighbor to which the LSR was sent.
     */
    void startLsrRetransmissionTimer(Neighbor& neighbor);

    /**
     * @brief Starts the LSU retransmission timer for a neighbor.
     *
     * Fires if a Link-State Update is not acknowledged within the retransmit
     * interval; triggers a re-flood of the unacknowledged LSAs.
     *
     * @param neighbor  The neighbor to which the LSU was sent.
     */
    void startLsuRetransmissionTimer(Neighbor& neighbor);

    /**
     * @brief Starts the LSR pacing timer to rate-limit request flooding.
     *
     * Prevents the LSR burst from saturating the neighbor immediately after
     * the Exchange state completes.
     *
     * @param neighbor  The neighbor whose LSR burst is being paced.
     */
    void startLsrPacingTimer(Neighbor& neighbor);

    /**
     * @brief Starts the LSU pacing timer to rate-limit update flooding.
     *
     * @param neighbor  The neighbor whose LSU burst is being paced, or
     *                  `nullptr` for interface-wide multicast pacing.
     */
    void startLsuPacingTimer(Neighbor* neighbor);

private:
    std::atomic<uint32_t> helloTimerId{0};              ///< Active Hello timer ID; 0 when not running.
    std::chrono::steady_clock::time_point helloStartTime; ///< When the current Hello interval began.

    std::atomic<bool> runTimers = true; ///< Set to false during teardown to suppress in-flight callbacks.

    core::ProcessQueueRef scheduler; ///< Scheduler used to post all timer callbacks.

    OspfInterface& iface; ///< The interface that owns this timer manager.
};

} // namespace routing::ospf

#endif // OSPF_INTERFACE_TIMERS_H
