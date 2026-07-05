/**
 * @file Originator.h
 * @brief Abstract base for OSPF LSA origination, throttling, and group-paced refresh.
 */

#ifndef OSPF_ORIGINATOR_H
#define OSPF_ORIGINATOR_H

#include "ospf/database/LSDB.hpp"

namespace core { class ProcessQueueRef; }
class Internal_OspfTest;

namespace routing::ospf
{
struct OspfInterfaceId;
class Area;
class OspfInterface;
class Neighbor;

/**
 * @brief Abstract base class that owns LSA origination, throttling, and group-paced refresh for one OSPF area.
 * @ingroup OSPF_AREA
 *
 * `Originator` centralises the rules for when and how the local router
 * generates new LSA instances.  It tracks the last-originated body for each
 * LSA key, implements an exponential back-off throttle to comply with the
 * MinLSInterval/MinLSArrival constraints in RFC 2328 §12.4, and organises
 * outstanding LSAs into RFC 2328 §12.4 group-pacing buckets to spread refresh
 * floods over time rather than sending all refreshes at once at 30-minute
 * boundaries.
 *
 * Concrete subclasses (`OriginatorV2`, `OriginatorV3`) implement the
 * version-specific link-state building logic — how Router LSA links are
 * encoded, how Network LSAs are formed, and how ASBR summary LSAs are
 * structured.
 *
 * Each instance contains:
 * - Per-LSA origination state and throttle timers (`originationState`)
 * - Group-pacing buckets with scheduled timer IDs (`refreshBuckets`, `keyToBucket`)
 * - Cache of the last-originated key for Router LSAs and sets for Network/ASBR LSAs
 * - Optional default route state for NSSA and stub areas
 *
 * ## Architectural Role
 * `Originator` sits between the `Area` (which detects topology events and
 * calls `updateInterface`, `addExternal`, etc.) and the `LSDB` / flood
 * pipeline (which receives LSA bodies via `processLsa`).  It does not perform
 * flooding itself; after computing a new LSA body it hands it to `processLsa`,
 * which inserts it into the LSDB and triggers flooding through the area.
 *
 * ## Lifecycle & Ownership
 * Created and owned by the `Area`.  The `Area` reference stored in the
 * protected `area` member must remain valid for the entire lifetime of the
 * originator.  `cancelGroupPacing()` must be called before destruction to
 * cancel all outstanding timer callbacks.
 *
 * ## Concurrency Model
 * All methods run on the area's single-threaded `ProcessQueue`.  The
 * `LsaThrottleState::pending` flag is `std::atomic` because the timer
 * callback may check it from a different thread before posting back onto the
 * process queue; all other state is single-threaded.
 *
 * @warning Subclasses must not call `processLsa` or modify `originationState`
 * from outside the area's process queue thread.
 *
 * @see Area
 * @see OriginatorV2
 * @see OriginatorV3
 */
class Originator
{
    friend class ::Internal_OspfTest;
public:

    /**
     * @brief Constructs the originator and binds it to its owning area.
     * @ingroup OSPF_AREA
     *
     * Does not originate any LSAs at construction time; the first origination
     * is triggered by the area calling `fullRefresh()` after the process starts.
     *
     * @param a The area that owns and will drive this originator.
     */
    Originator(Area& a);

    /**
     * @brief Destructs the originator.
     *
     * Does not cancel outstanding timers — callers must invoke
     * `cancelGroupPacing()` before destruction to ensure no timer callbacks
     * fire against a destroyed object.
     */
    virtual ~Originator();

    // PUBLIC ORIGINATIONS

    /**
     * @brief Re-originates all LSAs held by this router in the area from scratch.
     *
     * Called at process startup, after a router-ID change, or after an area
     * reconfiguration that invalidates all previously originated LSAs.
     * Implementations should recompute and resubmit every LSA type the router
     * is responsible for.
     */
    virtual void fullRefresh() = 0;

    /**
     * @brief Re-evaluates and re-originates LSAs affected by a change on the given interface.
     *
     * Called when an interface state machine transitions (e.g. Down→DR,
     * DROther→Backup) or when interface cost changes.  Implementations
     * re-examine the interface and update the Router LSA and, if the interface
     * is a DR segment, the Network LSA.
     *
     * @param ifaceId Interface index that changed.
     */
    virtual void updateInterface(uint32_t ifaceId) = 0;

    /**
     * @brief Originates or flushes an ASBR-summary or external LSA for a redistributed route.
     *
     * @param asbr   Router ID of the ASBR that introduced the external route.
     * @param lsid   Link-State ID to use for the new LSA.
     * @param expire If true, the LSA is being withdrawn (age set to MaxAge).
     */
    virtual void addExternal(uint32_t asbr, uint32_t lsid, bool expire) = 0;

    /**
     * @brief Translates an NSSA external LSA into a Type-5 external LSA for flooding beyond the NSSA.
     *
     * Called on the NSSA-ABR that is responsible for translating Type-7 LSAs
     * into Type-5 LSAs.  The translated LSA is re-originated with the key
     * derived from the source NSSA LSA.
     *
     * @param key    Key of the source NSSA LSA being translated.
     * @param lsa    Decoded body of the source NSSA LSA.
     * @param expire If true, the translation is being withdrawn.
     */
    virtual void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire) = 0;

    /**
     * @brief Originates or withdraws the default route summary LSA injected into a stub area.
     *
     * @param add True to originate the summary default; false to withdraw it.
     */
    virtual void addStubDefaultRoute(bool add) = 0;

    /**
     * @brief Originates or withdraws an inter-area summary LSA for the given prefix.
     *
     * Used by ABRs to summarise intra-area prefixes into adjacent areas.
     *
     * @param lsid   Link-State ID for the summary LSA.
     * @param prefix Network prefix being summarised.
     * @param cost   Metric to advertise in the summary.
     * @param expire If true, the summary is being withdrawn.
     */
    virtual void originateSummary(uint32_t lsid, const types::IPPrefix& prefix, uint32_t cost, bool expire = false) = 0;

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

    /**
     * @brief Originates or withdraws the NSSA default route for this area.
     *
     * The NSSA default is a Type-7 LSA injected by an NSSA ABR.  When `add`
     * is true and no NSSA default is currently installed, one is originated.
     * When false the existing default (if any) is flushed.
     *
     * @param add True to originate, false to withdraw.
     */
    void nssaDefaultOriginate(bool add);

protected:

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

private:
    /**
     * @brief Finalises and installs a re-originated LSA after the throttle delay has elapsed.
     *
     * Called from `runReorigination` once the throttle back-off has expired.
     * Reads the latest body from `originationState` and calls `processLsa`.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param key     Key of the LSA being re-originated.
     * @param info    Current origination state (body, expire flag, throttle info).
     */
    template<typename Policy>
    void processReoriginatedLsa(const LsaKey& key, const OriginationInfo& body);

protected:
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
     * @brief Handles a group-pacing bucket firing: re-originates all LSAs in the bucket.
     *
     * Called from `handleGroupPackingBucket` after the bucket's timer fires.
     * Each LSA in the bucket is refreshed by computing its current body and
     * calling `originateLsa`.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param key     Key of the LSA whose group-paced refresh is due.
     */
    template <typename Policy>
    void processOriginatedLsa(const LsaKey& key);

    // ADDING (version-specific)

    /**
     * @brief Builds and submits a new Router LSA instance.
     *
     * @param id          Optional override for the Link-State ID (normally the router ID).
     * @param refresh     True if this is a periodic refresh rather than a topology-driven update.
     * @param fullRefresh True if all links must be recomputed unconditionally.
     */
    virtual void addRouterLsa(std::optional<uint32_t> id, bool refresh, bool fullRefresh = false) = 0;

    /**
     * @brief Builds and submits a new Network LSA for the given DR interface.
     *
     * @param iface   The DR interface for which to originate a Network LSA.
     * @param refresh True if this is a periodic refresh.
     */
    virtual void addNetworkLsa(const OspfInterface& iface, bool refresh) = 0;

    /**
     * @brief Originates an ASBR-summary LSA for the given external router.
     *
     * @param asbr    Router ID of the ASBR being summarised.
     * @param refresh True if this is a periodic refresh.
     */
    virtual void addAsbrLsa(uint32_t asbr, bool refresh) = 0;

    // REMOVING (version-specific)

    /**
     * @brief Flushes the Network LSA associated with the given interface (MaxAge it).
     *
     * Called when the local router loses DR status on `ifaceId`.
     *
     * @param ifaceId Interface index whose Network LSA should be flushed.
     */
    virtual void removeNetworkLsa(uint32_t ifaceId) = 0;

    /**
     * @brief Sets the LSA identified by `key` to MaxAge and submits it for flooding.
     *
     * @param key Key of the LSA to expire.
     */
    virtual void expire(LsaKey& key) = 0;

    // GROUP PACING

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
     * @brief Timer callback that fires when a group-pacing bucket's interval expires.
     *
     * Re-originates all LSAs in `bucketIndex` and reschedules the bucket timer
     * for the next cycle.
     *
     * @tparam Policy Version policy selecting LSA type constants.
     * @param tid         Timer ID of the firing callback (used for validation).
     * @param bucketIndex Index into `refreshBuckets` of the bucket that fired.
     */
    template <typename Policy>
    void handleGroupPackingBucket(uint32_t tid, uint32_t bucketIndex);

    /**
     * @brief Cancels all outstanding group-pacing bucket timers.
     *
     * Must be called during area teardown, before the `Originator` is
     * destroyed, to prevent timer callbacks from firing against freed memory.
     */
    void cancelGroupPacing();

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

    // DEFAULT ROUTES

    std::optional<uint32_t> nssaDefaultRoute = std::nullopt; ///< Link-State ID of the currently originated NSSA default LSA, if any.
    std::optional<LsaKey>   stubDefaultRoute  = std::nullopt; ///< Key of the currently originated stub-area default summary LSA, if any.

    // LSA CACHE STORAGE

    LsaAdvKey                                    lastRouterKey{};       ///< Key of the most recently originated Router LSA.
    std::unordered_set<uint32_t>                 networkLsas{};         ///< Interface IDs for which this router holds an active Network LSA.
    std::unordered_map<uint32_t, LsaKey>         asbrLsas{};            ///< ASBR router ID → key of the ASBR-summary LSA originated for that ASBR.
    std::unordered_map<uint32_t, std::vector<uint32_t>> externalRoutes{}; ///< ASBR router ID → list of Link-State IDs for external LSAs originated for that ASBR.
    std::unordered_map<LsaKey, OriginationInfo>  originationState;      ///< Per-LSA throttle and body state for all in-flight or deferred originations.

    // GROUP PACING

    std::vector<RefreshBucket>           refreshBuckets; ///< Fixed-size array of pacing buckets; size set by initGroupPacing().
    std::unordered_map<LsaKey, uint32_t> keyToBucket;    ///< Maps each enrolled LSA key to its assigned bucket index.

    /**
     * @brief Installs an originated LSA into the LSDB and triggers flooding.
     *
     * Called by `runReorigination` and by subclasses after computing a new
     * LSA body.  Writes the record via the area's `LsdbTable` and posts a
     * flood event to all appropriate interfaces.
     *
     * @param key  Key of the LSA to install.
     * @param body Decoded body to store.
     */
    void processLsa(LsaKey& key, LsaBody& body);

    // LINK BUILDING HELPERS

    /**
     * @brief Adds the appropriate Router LSA link for the given interface to the in-progress LSA body.
     *
     * Selects transit, point-to-point, stub, or virtual-link encoding based on
     * the interface state machine state and neighbor adjacency status.  May
     * also trigger Network LSA origination if the interface is in DR state.
     *
     * @param router          In-progress Router LSA body being built.
     * @param iface           Interface to add a link for.
     * @param refresh         True if this is a periodic refresh build.
     * @param attemptNetLsa   If true and the interface is DR, attempt to originate a Network LSA.
     */
    void addRouterLink(LsaBody& router, const OspfInterface& iface, bool refresh, bool attemptNetLsa = false);

    /**
     * @brief Adds a transit (broadcast/NBMA DR-segment) link to the Router LSA body.
     *
     * @param router In-progress Router LSA body.
     * @param iface  DR interface to encode as a transit link.
     * @param nbr    Designated Router neighbor (nullptr if the local router is the DR).
     */
    virtual void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) = 0;

    /**
     * @brief Adds a point-to-point link to the Router LSA body.
     *
     * @param router   In-progress Router LSA body.
     * @param iface    Point-to-point interface.
     * @param neighbor The fully adjacent neighbor on this interface.
     */
    virtual void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) = 0;

    /**
     * @brief Adds a stub (non-transit) network link to the Router LSA body.
     *
     * Stub links represent directly connected subnets with no OSPF neighbor or
     * on interfaces that have not reached Full adjacency.
     *
     * @param router   In-progress Router LSA body.
     * @param iface    Interface to encode as a stub link.
     * @param fullMask If true, encode the link with a /32 (host) mask instead of the interface prefix mask.
     */
    virtual void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) = 0;

    /**
     * @brief Adds a virtual link to the Router LSA body.
     *
     * @param router In-progress Router LSA body.
     * @param iface  Virtual-link interface.
     * @param vNbr   The virtual-link neighbor (must be in Full state).
     */
    virtual void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) = 0;

    /**
     * @brief Removes duplicate adjacent links from a Router LSA link list.
     *
     * Applied after all links are collected to ensure that the same
     * (type, ID, data) tuple does not appear more than once in the LSA.
     *
     * @tparam RouterLink  Router LSA link entry type; must be equality-comparable.
     * @param links        Vector of links to deduplicate in-place.
     */
    template <typename RouterLink>
    void uniqueLinks(std::vector<RouterLink>& links);

protected:

    Area& area; ///< Owning OSPF area; provides access to the LSDB, interfaces, and scheduler.
};

template <typename RouterLink>
void Originator::uniqueLinks(std::vector<RouterLink>& links)
{
    links.erase(std::unique(links.begin(), links.end()), links.end());
}
} // namespace routing

#endif
