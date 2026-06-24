/**
 * @file TimerManager.h
 * @brief EIGRP timer management: Hello, Hold, Active timers.
 */

#ifndef EIGRP_TIMER_MANAGER_H
#define EIGRP_TIMER_MANAGER_H

#include <IPAddress.h>
#include <ControlScheduler.h>

namespace core
{
class Global;
class ProcessQueue;
}

namespace routing::eigrp
{
struct OutgoingQuery;
class DuelEngine;
struct TopologyEntry;
class Eigrp;
class Neighbor;

/**
 * @brief Schedules and cancels EIGRP protocol timers on behalf of the DUAL engine.
 * @ingroup EIGRP_TOPOLOGY
 *
 * `TimerManager` is the single point through which the EIGRP process posts
 * time-delayed callbacks to the process scheduler.  Currently it manages the
 * Stuck-In-Active (SIA) timer, which fires when a neighbor has not responded
 * to a query within the configured active-time window.
 *
 * ## Architectural Role
 * Owned by @ref DuelEngine.  When DUAL transitions a prefix to the Active
 * state and sends a Query to a neighbor, it calls `startSIATimer` to arm a
 * watchdog.  If the SIA timer fires before a Reply is received, DUAL tears
 * down the neighbor as unresponsive.  `cancelSIATimer` is called when a
 * Reply arrives before the timer fires.
 *
 * All timer callbacks are posted to `scheduler`, ensuring they execute on
 * the same thread as all other EIGRP state mutations and require no
 * additional locking.
 *
 * ## Lifecycle & Ownership
 * Created inside `DuelEngine` at process construction.  Both `base` and
 * `scheduler` references must outlive this object.
 *
 * @see DuelEngine
 * @see OutgoingQuery
 */
class TimerManager
{
public:
    /**
     * @brief Constructs a timer manager bound to the given process and scheduler.
     *
     * @param base      The owning EIGRP process; used to read the configured SIA timeout.
     * @param scheduler Process work queue to which timer callbacks are posted.
     */
    TimerManager(Eigrp& base, core::ProcessQueue& scheduler);

    /**
     * @brief Arms the SIA watchdog timer for a pending query to a neighbor.
     *
     * Posts a delayed callback on `scheduler` that will fire after the
     * configured active-time window expires.  If the timer fires,
     * `DuelEngine::handleSIATimeout` is called with `entry` and `neighbor`.
     *
     * @param entry    The outgoing query record tracking this SIA exchange.
     * @param neighbor The neighbor to which the query was sent.
     *
     * @note Calling this when a timer is already running for `entry` will
     *       replace the existing timer.
     */
    void startSIATimer(OutgoingQuery& entry, Neighbor& neighbor);

    /**
     * @brief Cancels a previously armed SIA timer.
     *
     * Should be called as soon as a Reply is received for the corresponding
     * query so the callback is never delivered.  Safe to call even if no
     * timer is currently armed for `entry`.
     *
     * @param entry The outgoing query record whose timer should be cancelled.
     */
    void cancelSIATimer(OutgoingQuery& entry);

private:

    Eigrp& base;                       ///< Owning process; provides SIA timeout configuration.
    core::ProcessQueueRef scheduler;   ///< Lifetime-safe ref used for timer callback delivery.
};
} // namespace routing::eigrp

#endif // EIGRP_TIMER_MANAGER_H
