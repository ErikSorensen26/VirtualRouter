/**
 * @file SpfManager.h
 * @brief OSPF SPF scheduler and recomputation triggers.
 */

#ifndef SPF_MANAGER_H
#define SPF_MANAGER_H

#include <ControlScheduler.h>
#include <chrono>
#include <cstdint>
#include <atomic>

#include "SpfTypes.hpp"
#include "SpfEngine.h"

namespace routing::ospf
{
class Area;
class OspfRib;

/**
 * @brief Schedules and throttles OSPF SPF recomputes for a single area.
 * @ingroup OSPF_SPF
 *
 * @c SpfManager sits between the OSPF event pipeline and @ref SpfEngine. When
 * a topology change is detected (LSA addition, withdrawal, or age-out),
 * protocol code calls @c requestSpf to signal that a recompute is needed.
 * @c SpfManager applies an exponential back-off delay before actually invoking
 * the engine, preventing SPF from running at full frequency during a topology
 * storm. Once the engine completes, the resulting @ref SpfResult is translated
 * into RIB updates via @c OspfRib.
 *
 * Each instance contains:
 * - A @ref SpfEngine that performs the actual Dijkstra computation.
 * - References to the owning @c Area and its @c OspfRib.
 * - Atomic state flags tracking whether a run is pending, scheduled, or active.
 * - A timer ID for the deferred scheduler callback.
 *
 * ## Architectural Role
 * @c SpfManager is the single point through which all SPF scheduling passes.
 * It decouples the "something changed" notification (fired on the LSDB
 * mutation path) from the "run the algorithm" execution (fired on a delayed
 * scheduler callback). This keeps the LSA processing fast path free of the
 * O(V log V) Dijkstra cost.
 *
 * ## Lifecycle & Ownership
 * Created and owned by @ref Area. Construction initialises all atomic flags to
 * idle and records the timer ID slot; no computation occurs until the first
 * @c requestSpf call. Destruction must occur after the scheduler has drained
 * any pending timer callbacks to avoid use-after-free on the @c area reference.
 *
 * ## Concurrency Model
 * @c requestSpf may be called from any thread that can post to the area
 * scheduler. The flags @c requested, @c spfScheduled, @c spfRunning, and
 * @c reschedule are all atomic to allow lock-free signalling from the caller.
 * The actual @c runSpf execution is always serialised on the area's control
 * scheduler; no two SPF runs overlap.
 *
 * @warning Never call @c runSpf directly. Always go through @c requestSpf so
 * that the throttle delay and scheduler serialisation are respected.
 *
 * @see SpfEngine
 * @see Area
 */
class SpfManager
{
public:
    /**
     * @brief Constructs an @c SpfManager bound to @p area.
     *
     * Initialises the engine and records references to the area and its RIB.
     * All atomic flags are initialised to their idle state. No SPF is scheduled
     * at construction; the first recompute is deferred until @c requestSpf is
     * called.
     *
     * @param area  OSPF area whose LSDB this manager recomputes.
     */
    explicit SpfManager(Area& area);

    /**
     * @brief Signals that a topology change has occurred and an SPF recompute is needed.
     *
     * If no SPF is currently scheduled, computes the next throttle delay via
     * @c computeNextDelay and posts a timer callback. If an SPF run is already
     * in progress, sets @c reschedule so that a follow-up run is triggered
     * automatically when the current one completes.
     *
     * @tparam Policy  Version-specific LSA policy (OSPFv2 or OSPFv3) forwarded
     *                 to the engine and topology builder.
     */
    template<typename Policy>
    void requestSpf();

    /**
     * @brief Timer callback invoked by the control scheduler when the SPF delay fires.
     *
     * Clears the scheduled flag and, if @c requested is still set, calls
     * @c runSpf. If a reschedule was requested while the run was in progress,
     * immediately calls @c requestSpf again.
     *
     * @tparam Policy  LSA policy forwarded to @c runSpf.
     */
    template<typename Policy>
    void onSpfTimer();

    SpfResult spfResult; ///< Most recent SPF output; updated after every successful run and read by route installation logic.

private:

    // SCHEDULING

    /**
     * @brief Posts a delayed timer on the area's control scheduler.
     *
     * Sets @c spfScheduled and arms the timer identified by @c timerId to fire
     * after @p delayMs milliseconds, at which point @c onSpfTimer is invoked.
     *
     * @tparam Policy  LSA policy forwarded to the timer callback.
     * @param  delayMs Delay in milliseconds before the SPF run is allowed to start.
     */
    template <typename Policy>
    void scheduleSpf(uint32_t delayMs);

    /**
     * @brief Computes the throttle delay for the next SPF run.
     *
     * Implements exponential back-off: the delay doubles on each successive
     * recompute until a configured maximum, then resets after the topology has
     * been stable for a hold-down interval. This prevents excessive CPU use
     * during instability while keeping convergence fast after the first event.
     *
     * @return Delay in milliseconds to wait before running SPF.
     */
    uint32_t computeNextDelay();

    /**
     * @brief Executes the SPF computation and installs the result into the RIB.
     *
     * Builds a @ref SpfTopology from the current LSDB, calls @ref SpfEngine::run,
     * stores the result in @c spfResult, and notifies @c OspfRib of route changes.
     * Clears @c spfRunning on completion. If @c reschedule was set during the run,
     * the caller (@c onSpfTimer) is responsible for posting a follow-up.
     *
     * @tparam Policy  Version-specific LSA policy (OSPFv2 or OSPFv3).
     */
    template <typename Policy>
    void runSpf();

private:

    SpfEngine engine; ///< Stateful Dijkstra engine; retains the previous result for incremental repair.

    Area&    area; ///< Owning OSPF area; provides the LSDB and scheduler.
    OspfRib& rib;  ///< Area RIB updated with each SPF result.

    std::atomic<bool> requested{false};    ///< Set by requestSpf; cleared when the run begins.
    std::atomic<bool> spfScheduled{false}; ///< True while a timer callback is pending.
    std::atomic<bool> spfRunning{false};   ///< True while runSpf is executing; prevents re-entry.
    std::atomic<bool> reschedule{false};   ///< Set if a new request arrives while a run is in progress.

    std::atomic<std::chrono::steady_clock::time_point> lastSpfTime; ///< Wall-clock time of the last completed run; drives the back-off reset logic.

    std::atomic<uint32_t> currentDelayMs{0}; ///< Most-recently applied throttle delay; doubles on each back-to-back request.
    uint32_t timerId;                         ///< Scheduler timer slot used for the deferred SPF callback.
};

} // namespace routing::ospf

#endif // SPF_MANAGER_H
