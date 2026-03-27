/**
 * @file Dampening.h
 * @brief BGP route flap dampening with penalty and reuse thresholds.
 */

/**
 * @defgroup BGP_FEATURES BGP Features
 * @ingroup BGP
 * @brief Optional BGP features: route flap dampening, graceful restart.
 */

#pragma once

#include <chrono>
#include <cmath>

namespace routing::bgp
{

/**
 * @brief Configuration parameters governing the dampening decay and suppression thresholds.
 * @ingroup BGP_FEATURES
 *
 * All fields correspond directly to the RFC 2439 dampening parameters that the
 * operator configures.  One instance of this struct is shared across all
 * @ref DampenState objects for a given BGP process or address family.
 */
struct DampenParams
{
    uint16_t halfLifeSecs;    ///< Half-life of the exponential penalty decay in seconds.
    uint16_t reuse;           ///< Penalty threshold below which a suppressed route becomes eligible for re-advertisement.
    uint16_t suppress;        ///< Penalty threshold above which a route is suppressed.
    uint16_t maxSuppressSecs; ///< Maximum time in seconds a route may remain suppressed regardless of penalty.
    double   ceiling;         ///< Maximum penalty value; prevents unbounded growth for perpetually flapping routes.
};

/**
 * @brief Per-prefix dampening state tracking penalty accumulation and suppression status.
 * @ingroup BGP_FEATURES
 *
 * Each unstable prefix in Adj-RIB-In that has ever been withdrawn gets one
 * DampenState entry.  The entry is driven by four events from the
 * @ref AddressFamilyInstance route processing loop:
 *
 * - @ref onWithdraw  — prefix transitions reachable → unreachable in Loc-RIB.
 * - @ref onAnnounce  — prefix transitions unreachable → reachable in Loc-RIB.
 * - @ref checkReuse  — periodic scan; transitions suppressed → eligible when
 *                      penalty decays below the reuse threshold.
 * - @ref isStale     — indicates the entry carries no meaningful history and
 *                      may be removed from the table to reclaim memory.
 *
 * ## Architectural Role
 * DampenState is a pure value type: it owns no resources and holds no pointers.
 * The penalty is decayed lazily on each state-changing call rather than on a
 * background timer, so the clock is only consulted when an event arrives or
 * the periodic reuse scan fires.
 *
 * ## Concurrency Model
 * DampenState is not thread-safe.  All calls must be serialized through the
 * owning @ref AddressFamilyInstance scheduler.
 *
 * @note The `pendingReuse` flag is set by the reuse-scan timer before calling
 * `recomputeNlri()` so that the subsequent route reappearance event is not
 * counted as another flap.  The caller must clear the flag after processing.
 *
 * @see DampenParams, AddressFamilyInstance
 */
struct DampenState
{
    double penalty       = 0.0;  ///< Current accumulated flap penalty; decays exponentially between events.
    bool   suppressed    = false; ///< True while the route is being withheld from advertisement due to excessive flapping.
    bool   everWithdrawn = false; ///< True once the prefix has been withdrawn at least once; gates penalty accumulation.
    bool   pendingReuse  = false; ///< Set by the reuse-scan timer to signal that the next announce is a reuse, not a new flap.

    std::chrono::steady_clock::time_point lastUpdate;    ///< Timestamp of the most recent penalty update; used to compute elapsed decay time.
    std::chrono::steady_clock::time_point suppressExpiry; ///< Absolute time after which suppression is lifted regardless of remaining penalty.

    /**
     * @brief Decay the penalty exponentially from @ref lastUpdate to @p now.
     *
     * Uses the half-life from @p halfLifeSecs to compute the decay factor.
     * Updates @ref lastUpdate to @p now.  No-op if @ref lastUpdate equals @p now.
     *
     * @param halfLifeSecs Half-life in seconds from the active @ref DampenParams.
     * @param now          Current monotonic clock time.
     */
    void decayTo(double halfLifeSecs, std::chrono::steady_clock::time_point now);

    /**
     * @brief Record a route announcement and determine whether to keep the route suppressed.
     *
     * If @ref pendingReuse is true the announcement is treated as a reuse event
     * (no additional penalty).  Otherwise the penalty is incremented by the
     * standard flap penalty and compared against @p p.suppress.
     *
     * @param p   Active dampening parameters for this address family.
     * @param now Current monotonic clock time.
     * @return True if the route should remain suppressed and must not be installed
     *         into Loc-RIB; false if the route is eligible for advertisement.
     */
    bool onAnnounce(const DampenParams& p, std::chrono::steady_clock::time_point now);

    /**
     * @brief Record a route withdrawal and determine whether to suppress further advertisements.
     *
     * Increments the penalty by the configured flap increment and checks whether
     * it now exceeds @p p.suppress.  Sets @ref suppressed and @ref suppressExpiry
     * if suppression begins.
     *
     * @param p   Active dampening parameters for this address family.
     * @param now Current monotonic clock time.
     * @return True if the route is now suppressed; false if the penalty remains
     *         below the suppression threshold.
     *
     * @note The caller should still withdraw the route from its peers regardless
     * of the return value; suppression only affects future re-advertisement.
     */
    bool onWithdraw(const DampenParams& p, std::chrono::steady_clock::time_point now);

    /**
     * @brief Check whether a suppressed route has decayed below the reuse threshold.
     *
     * Called periodically by the reuse-scan timer.  If the route is suppressed
     * and either the penalty has decayed below @p p.reuse or the suppression
     * expiry time has passed, clears @ref suppressed.
     *
     * @param p   Active dampening parameters for this address family.
     * @param now Current monotonic clock time.
     * @return True if the route just transitioned from suppressed to eligible;
     *         the caller should set @ref pendingReuse = true and re-trigger
     *         `recomputeNlri()` to re-advertise the route.
     */
    bool checkReuse(const DampenParams& p, std::chrono::steady_clock::time_point now);

    /**
     * @brief Return true when this entry holds no significant dampening history.
     *
     * An entry is stale when it is not suppressed and the accumulated penalty
     * has decayed to a negligible value (below 1.0).  Stale entries may be
     * removed from the dampening table to bound memory usage.
     */
    bool isStale() const noexcept { return !suppressed && penalty < 1.0; }
};

} // namespace routing::bgp
