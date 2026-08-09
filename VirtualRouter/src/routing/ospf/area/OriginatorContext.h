/**
 * @file OriginatorContext.h
 * @brief Per-area LSA origination mechanism: throttling, group pacing, and the install path.
 */

#ifndef OSPF_ORIGINATION_CONTEXT_H
#define OSPF_ORIGINATION_CONTEXT_H

#include <atomic>
#include <chrono>
#include "ospf/database/LsdbTypes.hpp"

class Internal_OspfTest;

namespace config::ospf { enum class AreaType : uint8_t; }
namespace config { struct OspfAreaRegistry; struct OspfRegistry; }

namespace routing::ospf
{
class Area;
class IntraOriginator;
class IntraOriginatorV2;
class IntraOriginatorV3;
class InterOriginator;
class InterfaceManager;
class ExternalOriginator;
class OpaqueOriginatorV2;
class GracefulRestartManager;
struct LsaKey;

/**
 * @brief Per-area LSA origination *mechanism*: throttle back-off, group-paced refresh, and submission into the LSDB/flood path.
 * @ingroup OSPF_AREA
 *
 * `OriginatorContext` is version-agnostic and policy-free.  It knows nothing
 * about *why* an LSA exists — the three policy classes decide that:
 * - @ref IntraOriginator builds this area's own topology LSAs (Router/Network).
 * - @ref InterOriginator (ABR role, process-scoped) injects inter-area summaries and area default routes.
 * - @ref ExternalOriginator (ASBR role, process-scoped) injects AS-External/NSSA-External LSAs.
 *
 * All three stage a body and flags into `originationState[key]` and call
 * @ref processOriginatedLsa, which enrols the key for group-paced refresh and
 * runs it through the exponential back-off throttle (RFC 2328 §12.4
 * MinLSInterval) before installing via `Area::processLsa`.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref Area as the `originContext` member.  The `area` reference must
 * remain valid for this object's entire lifetime.  `cancelGroupPacing()` must
 * be called before destruction to cancel all outstanding timer callbacks.
 *
 * ## Concurrency Model
 * All methods run on the owning process's single-threaded `ProcessQueue`, so
 * the process-scoped Inter/External originators may safely touch this per-area
 * state.  The `LsaThrottleState::pending` flag is `std::atomic` because the
 * timer callback may check it from a different thread before posting back onto
 * the process queue; all other state is single-threaded.
 *
 * @note Group-paced refresh must rebuild version-specific bodies
 * (Router/Network/ASBR-summary LSAs), so `handleGroupPacingBucket` dispatches
 * those types back to the owning area's @ref IntraOriginator and the process's
 * @ref InterOriginator.  This is the one place the mechanism calls back into
 * the policy classes.
 *
 * @see Area, IntraOriginator, InterOriginator, ExternalOriginator
 */
class OriginatorContext
{
private:
    friend class ::Internal_OspfTest;

    friend IntraOriginator;
    friend IntraOriginatorV2;
    friend IntraOriginatorV3;
    friend InterOriginator;
    friend ExternalOriginator;
    friend OpaqueOriginatorV2;
    friend GracefulRestartManager;

public:
    /**
     * @brief Constructs the origination state bound to its owning area.
     *
     * Does not schedule any timers; group pacing starts when the owning area
     * calls `initGroupPacing()`.
     *
     * @param a The area that owns this origination state.
     */
    explicit OriginatorContext(Area& a);

    /**
     * @brief Destroys the origination context.
     *
     * Cancels all group-pacing bucket timers and any armed throttle back-off
     * timers so no callback can fire against this object after it is freed.
     */
    ~OriginatorContext();

    /**
     * @brief Submits an LSA for origination, passing it through the throttle and LSDB install path.
     *
     * The key's throttle state is consulted: if enough time has passed since
     * the last origination the LSA is installed immediately; otherwise a timer
     * is armed to fire when the back-off expires.  The body and `expire` flag
     * are stored in `originationState` so the delayed callback can re-read them.
     *
     * @tparam Policy  Version policy (`PolicyV2` or `PolicyV3`) that selects the
     *                 correct LSA type constants and body types for this address family.
     * @param key      Full LSA key identifying the LSA to originate.
     * @param body     Decoded body to install.
     * @param expire   If true, the LSA's age is set to MaxAge before install (flush).
     */
    template <typename Policy>
    void originateLsa(const LsaKey& key, const LsaBody& body, bool expire);

    // LIFECYCLE (driven by the owning Area)

    /**
     * @brief Initialises the group-pacing bucket array and schedules all bucket timers.
     *
     * Must be called once after the process starts.  The number and spacing of
     * buckets is derived from the LSA refresh interval and bucket count defined
     * in the OSPF configuration.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     */
    template <typename Policy>
    void initGroupPacing();

    /**
     * @brief Cancels all outstanding group-pacing bucket timers.
     *
     * Must be called during area teardown, before this object is destroyed,
     * to prevent timer callbacks from firing against freed memory.
     */
    void cancelGroupPacing();

    /** @brief Cancels all owned timers (group-pacing buckets + per-LSA throttle callbacks); ~OspfProcess must call this before interface teardown to stop them racing it. */
    void cancelAllTimers();

private:

    /**
     * @brief Per-LSA exponential back-off throttle state.
     *
     * Tracks whether a re-origination is pending, the current back-off
     * interval, the next allowed fire time, and the timer ID of any outstanding
     * delayed-origination callback.
     */
    struct LsaThrottleState
    {
        std::atomic<bool> pending = false; ///< True if a re-origination is waiting for the back-off to expire.

        uint32_t backoffMs = 0;                                    ///< Current back-off interval in milliseconds; doubles on each re-arm.
        std::chrono::steady_clock::time_point nextFire{};          ///< Earliest wall-clock time the pending origination may fire.
        std::chrono::steady_clock::time_point lastOriginate{};     ///< When this LSA was last successfully originated.
        uint32_t timerId = 0;                                      ///< Timer handle for the pending back-off callback (0 = no timer).
    };

    /**
     * @brief Persistent state for one LSA undergoing throttled re-origination.
     * @ingroup OSPF_AREA
     *
     * Stored in `originationState` for the duration between the triggering
     * event and the actual install.  The `body` field holds the most recent
     * version computed by the originator; if `pending` is set and a new
     * topology event arrives before the timer fires, the body is updated in
     * place so only one origination occurs once the throttle expires.
     */
    struct OriginationInfo
    {
        LsaThrottleState throttleInfo;
        LsaBody body = std::monostate{};
        bool refresh = false; ///< True if this pending origination is a periodic refresh (body unchanged).
        bool expire  = false; ///< True if this pending origination should flush the LSA (MaxAge).
    };

    /**
     * @brief One time-based refresh bucket for LSA group pacing.
     * @ingroup OSPF_AREA
     *
     * RFC 2328 §12.4 recommends spreading the 30-minute LSA refresh cycle
     * across multiple intervals to avoid synchronised flooding.  Each bucket
     * holds the timer ID for its scheduled callback and the set of LSA keys
     * whose refresh should fire at that bucket's time.
     */
    struct RefreshBucket
    {
        uint32_t timerId = 0;           ///< Timer handle for the bucket's scheduled callback (0 = not scheduled).
        std::vector<LsaKey> keys;       ///< LSA keys assigned to this bucket.
    };

    // GROUP PACING

    /**
     * @brief Assigns an LSA to a group-pacing bucket so it will be periodically refreshed.
     *
     * The bucket is chosen by hashing the key to distribute LSAs evenly.
     *
     * @param key Key of the LSA to enrol in group pacing.
     */
    void scheduleForGroupPacing(const LsaKey& key);

    /**
     * @brief Removes an LSA from its assigned group-pacing bucket.
     *
     * Called when an LSA is flushed (MaxAge) so it is no longer refreshed.
     *
     * @param key Key of the LSA to remove from group pacing.
     */
    void unscheduleForGroupPacing(const LsaKey& key);

    /**
     * @brief Timer callback that fires when a group-pacing bucket's interval expires.
     *
     * Re-originates all LSAs in `bucketIndex` and reschedules the bucket timer
     * for the next cycle.  Router, Network, and ASBR-summary LSAs are
     * dispatched back to the policy classes for a version-specific rebuild;
     * all other types are re-submitted from their stored bodies.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param tid         Timer ID of the firing callback (used for validation).
     * @param bucketIndex Index into `refreshBuckets` of the bucket that fired.
     */
    template <typename Policy>
    void handleGroupPacingBucket(uint32_t tid, uint32_t bucketIndex);

    // THROTTLED (RE)ORIGINATION

    /**
     * @brief Schedules a re-origination of the LSA identified by `state` after the throttle back-off.
     *
     * If the back-off has not yet elapsed, arms a timer to call
     * `runReorigination` when it expires.  If the LSA is eligible for
     * immediate origination, calls `runReorigination` directly.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param state   Key of the LSA to re-originate.
     */
    template <typename Policy>
    void requestReorigination(const LsaKey& state);

    /**
     * @brief Executes a pending re-origination, updating the throttle state and calling `processLsa`.
     *
     * Called either directly from `requestReorigination` (when no throttle
     * delay is needed) or from the back-off timer callback.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param key     Key of the LSA to originate.
     */
    template <typename Policy>
    void runReorigination(const LsaKey& key);

    /**
     * @brief Finalises and installs a re-originated LSA after the throttle delay has elapsed.
     *
     * Called from `runReorigination` once the throttle back-off has expired.
     * Reads the latest body from `originationState` and installs it via
     * `Area::processLsa`, handling sequence-number rollover (RFC 2328 §12.1.6).
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param key     Key of the LSA being re-originated.
     * @param info    Current origination state (body, expire flag, throttle info).
     */
    template<typename Policy>
    void processReoriginatedLsa(const LsaKey& key, const OriginationInfo& info);

    /**
     * @brief Runs a freshly staged LSA through the origination pipeline.
     *
     * Called after `originateLsa` stores the body in `originationState`:
     * enrols the key for group-paced refresh, then requests a throttled
     * (re)origination which installs it via `Area::processLsa`.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param key     Key of the LSA to originate.
     */
    template <typename Policy>
    void processOriginatedLsa(const LsaKey& key);

    /**
     * @brief Forwards a received AS-External/NSSA-External LSA to the owning area's external install path.
     *
     * Thin pass-through to `Area::processExternalLsa`, exposed here so the
     * process-scoped @ref ExternalOriginator can reach it without being a
     * friend of `Area`.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Incoming LSA context (key, header, flood info).
     * @param body    Decoded external LSA body.
     */
    template <typename Policy>
    void processExternalLsa(IncomingLsaContext& ctx, const LsaBody& body);

    // HELPERS

    IntraOriginator& getIntraOriginator();
    InterOriginator& getInterOriginator();
    ExternalOriginator& getExternalOriginator();
    OpaqueOriginatorV2* getOpaqueOriginator();

    uint32_t getAreaFlags() const;

    const config::OspfAreaRegistry& getConfigs() const;
    const config::OspfRegistry& getProcessConfigs() const;

    bool isValidForwardAddress(const types::IPAddress& addr) const;

    const InterfaceManager& getIfaceMgr() const;

    // ASBR REACHABILITY TRACKING

    std::unordered_map<uint32_t, LsaKey>         asbrLsas{};            ///< ASBR router ID → key of the ASBR-summary LSA originated for that ASBR.
    std::unordered_map<uint32_t, std::vector<uint32_t>> asbrExternalRoutes{}; ///< ASBR router ID → list of Link-State IDs for external LSAs originated for that ASBR.

    // DEFAULT ROUTES

    std::optional<uint32_t> nssaDefaultRoute = std::nullopt; ///< Link-State ID of the currently originated NSSA default LSA, if any.
    std::optional<LsaKey>   stubDefaultRoute = std::nullopt; ///< Key of the currently originated stub-area default summary LSA, if any.

    // ORIGINATION / GROUP PACING STATE

    std::unordered_map<LsaKey, OriginationInfo>  originationState;      ///< Per-LSA throttle and body state for all in-flight or deferred originations.
    std::vector<RefreshBucket>           refreshBuckets; ///< Fixed-size array of pacing buckets; size set by initGroupPacing().
    std::unordered_map<LsaKey, uint32_t> keyToBucket;    ///< Maps each enrolled LSA key to its assigned bucket index.

    Area& area; ///< Owning OSPF area; provides the scheduler, configuration, and the `processLsa` install path.
};
}

#endif // OSPF_ORIGINATION_CONTEXT_H
