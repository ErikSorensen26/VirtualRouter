/**
 * @file Area.h
 * @brief OSPF area object: owns the LSDB, SPF manager, flood manager, and intra-area originator for one area.
 */

/**
 * @defgroup OSPF_AREA OSPF Area
 * @ingroup OSPF
 * @brief Area object, origination context, intra-area originator, and intra-area route manager.
 */

#ifndef OSPF_AREA_H
#define OSPF_AREA_H

#include <optional>

#include "ospf/database/LsdbTable.h"
#include "ospf/spf/SpfManager.h"
#include "ospf/FlagManager.hpp"
#include "IntraRouteManager.h"
#include "ospf/flooding/FloodManager.h"
#include "OriginatorContext.h"
#include "ospf/ospfv2/area/OpaqueOriginatorV2.h"

namespace config::ospf { enum class AreaType : uint8_t; }
namespace config { struct OspfInterfaceBaseRegistry; }
class Internal_OspfTest;

namespace routing::ospf
{
struct OspfPath;
struct RouteManagerUtility;
class OspfProcess;
class OspfInterfaceBase;
class InterfaceManager;
class OspfRib;
class TopologyTable;
class GracefulRestartManager;

/**
 * @brief Returns true if the LSA has reached or exceeded MaxAge.
 *
 * @param h      LSA header whose age field is tested.
 * @param maxAge Protocol-defined maximum age (see @ref OSPF_MAX_AGE).
 */
constexpr inline bool isMaxAge(const LsaHeader& h, uint16_t maxAge) noexcept
{
    return h.age >= maxAge;
}

/**
 * @brief Computes the absolute difference between two unsigned 16-bit ages without wrapping.
 *
 * Used to compare LSA ages when deciding whether one instance is newer than
 * another (RFC 2328 §13.1 step 3).
 *
 * @param a First age value.
 * @param b Second age value.
 * @return Absolute difference, saturated to uint16_t range.
 */
constexpr inline uint16_t absDiffU16(uint16_t a, uint16_t b) noexcept
{
    return (a >= b) ? static_cast<uint16_t>(a - b) : static_cast<uint16_t>(b - a);
}

/**
 * @brief Result of LSA wire-format calculations (checksum and total length).
 * @ingroup OSPF_AREA
 */
struct CalcResults
{
    uint16_t checksum; ///< Fletcher-16 checksum covering the LSA header and body.
    uint16_t size;     ///< Total wire size of the LSA in bytes (20-byte header + body).
};

/**
 * @brief Computes the Fletcher-16 checksum and wire length for a formatted LSA.
 *
 * Iterates over the header fields and delegates body serialization to
 * `lsa.appendChecksum(check)` so the computation is policy-independent.
 *
 * @tparam Policy  PolicyV2 or PolicyV3 — controls whether the OSPFv2 options
 *                 byte is included in the checksum calculation.
 * @param hdr  LSA header fields (sequence, age, etc.).
 * @param key  LSA identity (type, Link-State ID, advertising router).
 * @param body Variant body whose active alternative provides `size()` and
 *             `appendChecksum()`.
 * @return     Populated @ref CalcResults with checksum and total wire length.
 */
template<typename Policy>
CalcResults runLsaCalculations(const LsaHeader& hdr, const LsaKey& key, const LsaBody& body)
{
    CalcResults res;

    std::visit([&](auto& lsa) {
        using T = std::decay_t<decltype(lsa)>;
        if constexpr (!std::is_same_v<T, std::monostate>)
        {
            // Finalize length
            res.size = 20 + lsa.size();

            ChecksumFletcher check;

            if constexpr (std::is_same_v<Policy, PolicyV2>)
                check.add(hdr.options);

            check.addU16(key.lsaType);
            check.addU32(key.linkStateId);
            check.addU32(key.advertisingRouter);
            check.addU32(hdr.sequence);
            check.addU16(res.size); // Length

            lsa.appendChecksum(check);

            res.checksum = check.finalize();
        }
    }, body);

    return res;
}

/**
 * @brief Single OSPF area: owns the LSDB, SPF manager, flood engine, and LSA originator.
 * @ingroup OSPF_AREA
 *
 * An Area corresponds to the RFC 2328 §10 concept of an OSPF area.  Each area
 * holds its own link-state database (@ref LsdbTable), runs its own Dijkstra
 * computation through @ref SpfManager, and manages flooding through
 * @ref FloodManager.
 *
 * ## Architectural Role
 * Area sits below @ref OspfProcess (which owns all areas) and above individual
 * interfaces (@ref OspfInterfaceBase).  It is the unit of topology isolation in OSPF:
 * LSAs do not cross area boundaries except via ABR summary origination.
 *
 * ## Lifecycle & Ownership
 * Constructed and destroyed by OspfProcess.  Construction registers an aging
 * timer; destruction cancels all timers before the LSDB is torn down.
 *
 * ## Concurrency Model
 * All mutable Area state is accessed on the owning OspfProcess scheduler
 * thread.  The `options` and `dcCompatible` members are atomic because they
 * may be read from the Hello-send path which runs on the same scheduler but
 * can race with config-change callbacks.
 *
 * @warning `releaseMemory()` must only be called when the LSDB is quiescent
 * (no outstanding LsaRecordRef objects held by flood or retransmission queues).
 * Releasing storage while live references exist causes undefined behavior.
 *
 * @see OspfProcess, LsdbTable, SpfManager, FloodManager, IntraOriginator, OriginatorContext
 */
class Area
{
    friend class ::Internal_OspfTest;
public:

    /**
     * @brief Constructs an OSPF area.
     * @ingroup OSPF_AREA
     *
     * Initializes the LSDB, SPF manager, flood manager, origination context,
     * and intra-area originator, then starts the LSA aging timer.
     *
     * @param base  Owning OspfProcess.
     * @param area  32-bit area identifier (host byte order).
     */
    Area(OspfProcess& base, uint32_t area);

    /**
     * @brief Destroys the area.
     *
     * Cancels all pending timers (aging, ignore, reset) before tearing down
     * the LSDB and flood manager to prevent timer callbacks from firing on
     * already-freed state.
     */
    ~Area();

    // PUBLIC GETTERS

    config::ospf::AreaType getType() const { return priv.type.load(std::memory_order_relaxed); }
    bool isDcCompatible() const { return priv.dcCompatible.load(std::memory_order_relaxed); }

    // ENQUEUE

    /**
     * @brief Prepares the area for a graceful reset without destroying it.
     *
     * Schedules a reset timer; the actual reset runs asynchronously so
     * in-flight LSA processing can complete first.
     */
    void enqueueReset();

    /**
     * @brief Schedules a reset driven by an area-type config change.
     *
     * Config-change entry point: called when the area's `AREA_TYPE` setting
     * is written. Passes the new type straight through to `reset()` so the
     * scheduler-thread work doesn't need to re-read the config.
     *
     * @param newType The area type just written to config.
     */
    void enqueueReset(config::ospf::AreaType newType);

    /**
     * @brief Schedules `syncRangeConfig()` on the process queue.
     *
     * Config-change entry point: called when the area's `range` configuration
     * changes so the summarization state is rebuilt on the scheduler thread.
     */
    void enqueueSyncRanges();

    /**
     * @brief Aggregated result returned after processing an incoming LSA.
     * @ingroup OSPF_AREA
     *
     * Callers inspect `decision.action` to decide whether to flood, fight-back,
     * or ignore the LSA, and use `record` to enqueue the updated entry.
     */
    struct Result final
    {
        LsaRecordFlags flags;          ///< Flags computed for the stored record (self-originated, checksum-valid, etc.).
        FloodReason reason;            ///< Why the LSA should be flooded (refresh, update, or flush).
        InstallResult decision{};      ///< Full install decision including action, compare result, and flood/fight-back signals.
        LsaRecord* record{nullptr};    ///< Pointer into the LSDB for the installed or existing record.
    };

    OspfProcess& process; ///< Reference pointing to the root process.
    const uint32_t areaId; ///< 32-bit OSPF area identifier (host byte order); immutable after construction.

private:
    friend class OspfRib;
    friend class OspfProcess;
    friend class OspfInterfaceBase;
    friend class InterfaceManager;
    friend class FloodManager;
    friend class SpfManager;
    friend class OriginatorContext;
    friend class GracefulRestartManager;
    friend struct RouteManagerUtility;

    // FLOODING

    /**
     * @brief Floods a batch of LSAs out of every interface in this area.
     *
     * @param records Batch of (FloodInfo, LsaRecordRef) pairs to transmit.
     */
    void send(std::vector<std::pair<FloodInfo, LsaRecordRef>>& records);

    // LSA PROCESSING

    /**
     * @brief Processes a received (mutable body) LSA against the area LSDB.
     *
     * Evaluates the incoming LSA (RFC 2328 §13), installs it if it is newer,
     * and returns a @ref Result describing the outcome and flood disposition.
     *
     * @tparam Policy  PolicyV2 or PolicyV3.
     * @param ctx      Incoming LSA context (key, header, flood info, interface).
     * @param body     Decoded LSA body (mutable overload; ownership may be transferred to LSDB).
     * @return         Result with install decision, or std::nullopt if the LSA was silently dropped.
     */
    template <typename Policy>
    std::optional<Result> processLsa(IncomingLsaContext& ctx, LsaBody& body);
/**
     * @brief Processes a received (const body) LSA against the area LSDB.
     *
     * Const overload for callers that cannot give up body ownership.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Incoming LSA context.
     * @param body    Decoded LSA body (read-only; copied into LSDB if installed).
     * @return        Result, or std::nullopt if dropped.
     */
    template <typename Policy>
    std::optional<Result> processLsa(IncomingLsaContext& ctx, const LsaBody& body);

    /**
     * @brief Installs a batch of summary LSAs originating from an ABR into this area's LSDB.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param summaries Map of LsaKey → decoded body for each summary to install.
     */
    template <typename Policy>
    void processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries);

    /**
     * @brief Processes a received AS-External or NSSA-External LSA.
     *
     * Routes the LSA to the process-wide external LSDB rather than the per-area
     * LSDB, then triggers SPF if the topology is affected.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx     Incoming LSA context.
     * @param body    Decoded external LSA body.
     */
    template <typename Policy>
    void processExternalLsa(IncomingLsaContext& ctx, const LsaBody& body);

    /**
     * @brief Checks whether an incoming LSA header is newer than the stored instance.
     *
     * Implements RFC 2328 §13.1 comparison using sequence, checksum, and age.
     *
     * @param hdr  Header from the incoming LSA.
     * @param key  Identity of the LSA to look up in the LSDB.
     * @return True if the incoming instance should replace the stored one.
     */
    bool compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const;

    // AREA RANGE (INTER-AREA SUMMARIZATION)

    /**
     * @brief Synchronizes `area range` configuration into the runtime range map.
     *
     * Reads the configured `area range` prefixes and rebuilds the `ranges` map,
     * triggering re-origination or withdrawal of inter-area summary LSAs.
     */
    void syncRangeConfig();

    /**
     * @brief Updates range contributor counts from the current SPF result set.
     *
     * Called after each SPF run to recount how many intra-area routes fall
     * under each configured range prefix, then re-computes aggregate metrics.
     *
     * @param pathList  Updated intra-area path list from the SPF run.
     * @param abrChange True if the ABR flag changed; forces re-evaluation of all ranges.
     */
    void syncRangeRuntime(const std::vector<std::pair<types::IPPrefix, OspfPath>>& pathList, bool abrChange = false);

    /**
     * @brief Suppresses or un-suppresses inter-area summary LSAs for ranges with no contributors.
     *
     * @param ranges     Set of range prefixes to evaluate.
     * @param abrChange  True if the ABR flag changed; forces full re-evaluation.
     */
    void syncRangeSuppression(const std::unordered_set<types::IPPrefix>& ranges, bool abrChange = false);

    /**
     * @brief Immediately withdraws the inter-area summary LSA for a specific prefix.
     *
     * Used when the last contributing route for a range disappears.
     *
     * @param prefix The aggregate prefix whose summary should be suppressed.
     */
    void suppressInterAreaPrefix(const types::IPPrefix& prefix);

    /**
     * @brief Returns the set of configured range prefixes for this area.
     */
    const std::unordered_set<types::IPPrefix>& getRanges() const;

    /**
     * @brief Checks whether an address qualifies as a valid OSPF forwarding address.
     *
     * A forwarding address is valid if it belongs to one of the interfaces in
     * this area and the interface is not configured as passive (RFC 2328 §16.4).
     *
     * @param h  Address to test.
     * @return True if `h` is a reachable, non-passive interface address in this area.
     */
    bool isValidForwardAddress(const types::IPAddress& h) const;

    // RESET & LIFECYCLE

    /**
     * @brief Performs a full area reset: flushes all self-originated LSAs and clears neighbor state.
     *
     * After a reset the area re-originates its Router LSA and rejoins adjacencies
     * as if freshly started.
     */
    void reset();

    /**
     * @brief Performs a full area reset using an already-known new area type.
     *
     * Same as `reset()` but passes `newType` straight to `reloadType()`
     * instead of having it re-read from config. Used by the `AREA_TYPE`
     * config-change path.
     *
     * @param newType The area type to reload with.
     */
    void reset(config::ospf::AreaType newType);

    /**
     * @brief Reloads the area type from configuration and synchronizes options flags.
     *
     * Called before reset() when the area type config changes so that the
     * E-bit/N-bit in Hello and DD packets, LSA filtering, and default route
     * origination all reflect the new type without destroying the Area object.
     */
    void reloadType();

    /**
     * @brief Reloads the area type using an already-known value and
     *        synchronizes options flags. See @ref reloadType.
     *
     * @param newType The area type to reload with.
     */
    void reloadType(config::ospf::AreaType newType);

    /**
     * @brief Shared body of `reset()`/`reset(AreaType)`: flushes self-originated
     *        LSAs and resets neighbor state. Run after `reloadType()` has
     *        already applied the (possibly unchanged) area type.
     */
    void resetCommon();

    /**
     * @brief Clears the LSDB and all runtime state without destroying the area object.
     */
    void clear();

    /**
     * @brief Releases all LSDB storage (records and indexes) in bulk.
     *
     * @warning Must only be called when no @ref LsaRecordRef objects are alive
     * that point into this area's LSDB.  Violating this causes use-after-free.
     */
    void releaseMemory();

    /**
     * @brief Validates Demand-Circuit LSA headers for consistency.
     *
     * Scans the LSDB for LSAs that claim DC compatibility but whose headers
     * indicate otherwise, logging any inconsistencies for operator awareness.
     */
    void runDCIntegrityScan();

    /**
     * @brief Flushes all LSAs originated by the given neighbor Router ID.
     *
     * Called when a neighbor drops from FULL state so that stale information
     * is not retained in the LSDB.
     *
     * @param neighborRid Router ID of the neighbor whose LSAs should be flushed.
     */
    void flushNeighborLsas(uint32_t neighborRid);


    /**
     * @brief Computes the @ref LsaRecordFlags to assign to a newly installed LSA.
     *
     * Sets SELF_ORIGINATED when the incoming LSA was sent by this router, and
     * CHECKSUM_VALID when the checksum field verified correctly.
     *
     * @param ctx Incoming LSA context.
     * @return Packed flags for the LSDB record.
     */
    static LsaRecordFlags makeFlags(const IncomingLsaContext& ctx) noexcept;

    /**
     * @brief Runtime state for a configured `area range` inter-area summarization prefix.
     *
     * Tracks both the operator-supplied configuration and the live contribution
     * count derived from intra-area routes covered by the range.  When
     * `contributorCount` drops to zero the aggregate summary LSA is withdrawn.
     */
    struct AreaRange
    {
        // CONFIG
        bool notAdvertise;                      ///< If true the range is suppressed (not advertised as a summary).
        std::optional<uint32_t> costOverride;   ///< Operator-specified metric override for the summary LSA; absent means use computed metric.

        // RUNTIME
        uint32_t contributorCount = 0;  ///< Number of intra-area routes currently covered by this range.
        uint32_t computedMetric = 0;    ///< Best metric among contributing routes; used when no costOverride is set.

        std::optional<uint32_t> summary = std::nullopt; ///< Link-State ID of the active summary LSA, if any.
        bool discardPresent = false;                    ///< True if a discard (null-route) has been installed in the RIB for this range.
    };

    LsdbTable lsdb;          ///< Link-state database for this area.
    SpfManager spfMgr;     ///< Dijkstra SPF engine for this area.
    AreaFlagManager flags;  ///< Tracks area-type flags (stub, NSSA, etc.) and propagates changes.
    FloodManager floodMgr; ///< Manages reliable LSA flooding within this area.

    // RANGES

    std::unordered_map<types::IPPrefix, std::pair<uint32_t, uint32_t>> computeRangeContributors(
        const std::vector<std::pair<types::IPPrefix, OspfPath>>& intraAreaRoutes,
        const std::unordered_map<types::IPPrefix, AreaRange>& ranges
    );

    core::ProcessQueue scheduler; ///< Reference to the owning process scheduler; all area work is serialized through this.
    OriginatorContext originContext; ///< Origination mechanism: throttle back-off, group-paced refresh, and the LSDB install path.
    std::optional<OpaqueOriginatorV2> opaqueOriginator; ///< OSPFv2-only opaque LSA originator (Router Capability); unset for V3 areas. Must be constructed before `originator` (its constructor drives a fullRefresh() that touches this).
    IntraOriginator& originator; ///< Version-specific intra-area originator (Router/Network LSAs); heap-allocated by IntraOriginator::create(), deleted in ~Area().
    IntraRouteManager routeManager; ///< Derives this area's intra-area prefix routes from SPF results.

    const config::OspfAreaRegistry& configs; ///< Area-level OSPF configuration reference.

    // ORIGINATOR HELPERS

    InterOriginator& getInterOriginator();
    ExternalOriginator& getExternalOriginator();
    OpaqueOriginatorV2* getOpaqueOriginator() { return opaqueOriginator ? &*opaqueOriginator : nullptr; }
    TopologyTable& getTopoTable();
    const config::OspfRegistry& getProcessConfigs() const;
    const InterfaceManager& getIfaceMgr() const;

private:
    struct Private
    {
    private:
        friend class Area;
        friend class ::Internal_OspfTest;

        Private(Area& area);

        /**
         * @brief Enforces the `max-lsa` database limit on a newly arriving LSA.
         *
         * Returns true if the LSA may be installed.  When the configured
         * ceiling is reached the LSA is ignored and the ignore/reset
         * escalation (RFC-style database overload protection) is started.
         */
        bool onNewLsa();

        /**
         * @brief Records one ignored LSA and escalates if the ignore count exceeds its limit.
         *
         * Bumps `ignoreSize`, requests a process reset once
         * `max-lsa ignore-count` is exceeded, and arms the ignore timer.
         */
        void ignoreLsa();

        /**
         * @brief Arms the `max-lsa ignore-time` timer if not already running.
         *
         * When it fires, the reset timer is started, deferring the area reset
         * until `max-lsa reset-time` has also elapsed.
         */
        void startIgnoreTimer();

        /**
         * @brief Arms the `max-lsa reset-time` timer if not already running.
         *
         * When it fires, a full process reset is enqueued to recover from
         * sustained database overload.
         */
        void startResetTimer();

        size_t ignoreSize{0};       ///< LSDB size threshold below which the ignore timer is suppressed.
        uint32_t ignoreTid{0};      ///< Timer handle for the LSA-ignore rate-limiting timer.
        uint32_t resetTid{0};       ///< Timer handle for the deferred area-reset timer.
        uint32_t agingTimerId{0};   ///< Timer handle for the one-second LSA aging tick.

        // LSA PROCESSING

        /**
         * @brief Writes the LSA into the LSDB according to the install decision (move overload).
         *
         * Applies the `max-lsa` gate via `onNewLsa()`, then upserts the record
         * for INSTALL/FLUSH/FIGHT_BACK actions, moving `body` into storage.
         *
         * @param result Install decision from `process()`; `record` is filled in.
         * @param ctx    Incoming LSA context.
         * @param body   Decoded body; ownership is transferred to the LSDB.
         */
        void installLsa(Result& result, const IncomingLsaContext& ctx, LsaBody& body);

        /**
         * @brief Writes the LSA into the LSDB according to the install decision (copy overload).
         *
         * Same as the move overload but copies `body` for callers that retain
         * ownership.
         */
        void installLsa(Result& result, const IncomingLsaContext& ctx, const LsaBody& body);

        /**
         * @brief Evaluates an incoming LSA against the stored instance and computes the install decision.
         *
         * Runs `evaluateIncomingLsa` (RFC 2328 §13 comparison) and handles the
         * duplicate/age-refresh bookkeeping; does not write a new body — that
         * is `installLsa`'s job.
         *
         * @param ctx  Incoming LSA context.
         * @param body Decoded body (used for topology-change comparison).
         * @return Result carrying the decision and a pointer to the stored record.
         */
        Result process(IncomingLsaContext& ctx, const LsaBody& body);

        /**
         * @brief Area-type admission filter run before any LSDB work (RFC 2328 / RFC 3101).
         *
         * Rejects summaries in totally-stubby areas, external LSAs in
         * stub/NSSA areas that must not carry them, and NSSA LSAs in normal
         * areas.
         *
         * @tparam Policy PolicyV2 or PolicyV3.
         * @return True if the LSA is admissible in this area.
         */
        template <typename Policy>
        bool preProcess(IncomingLsaContext& ctx, const LsaBody& body);

        /**
         * @brief Post-install reactions to an accepted LSA.
         *
         * On topology-affecting installs: re-checks Demand-Circuit
         * compatibility (Router/Network LSAs), triggers NSSA translation
         * (external LSAs), and re-derives inter-area routes / summaries
         * (Type-3/Type-4 LSAs).
         *
         * @tparam Policy PolicyV2 or PolicyV3.
         */
        template <typename Policy>
        void postProcess(Result& result, IncomingLsaContext& ctx, const LsaBody& body);

        /**
         * @brief Determines the flood disposition for an already-installed LSA.
         *
         * Called after @ref processLsa to fill in the `reason` and flood signals
         * in `decision` based on context flags (self-originated, MaxAge, etc.).
         *
         * @tparam Policy   PolicyV2 or PolicyV3.
         * @param[out] decision  Result struct to update with flood reasoning.
         * @param ctx       Context for the incoming LSA.
         */
        void evaluateDecision(Result& decision, const IncomingLsaContext& ctx);


        /**
         * @brief Arms the one-second LSA aging tick.
         */
        void startAgingTimer();

        /**
         * @brief One-second aging tick: ages all LSAs, floods and purges MaxAge entries.
         *
         * Any expiry triggers an SPF request.  Re-arms the aging timer at the
         * end of every tick.
         */
        void onAgingTick();


        /**
         * @brief Full RFC 2328 §13 install decision for one incoming LSA.
         *
         * Handles checksum rejection, missing-entry install, MaxAge flush,
         * MinLSArrival rate limiting, self-origination fight-back, and
         * newer/older/duplicate comparison against `existing`.
         *
         * @param existing Stored record for the same key, or nullptr.
         * @param ctx      Incoming LSA context.
         * @param body     Decoded body (compared to detect topology changes).
         * @return The computed install action and flood/SPF signals.
         */
        InstallResult evaluateIncomingLsa(const LsaRecord* existing, IncomingLsaContext& ctx, const LsaBody& body);

        /**
         * @brief RFC 2328 §13.1 header comparison: sequence, checksum, then age.
         *
         * @return NEWER if `a` is more recent than `b`, OLDER if less recent,
         *         SAME if they are indistinguishable.
         */
        LsaCompareResult compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const;

        /**
         * @brief Returns true if the two decoded bodies differ (topology changed).
         *
         * Falls back to "changed" when the active alternatives differ or the
         * body type provides no equality operator.
         */
        bool compareLsaBody(const LsaBody& a, const LsaBody& b);

        std::atomic<uint8_t> options; ///< Area options byte advertised in Hello and DD packets; updated atomically.
        std::atomic<config::ospf::AreaType> type;        ///< Area type (backbone, stub, NSSA, etc.); updated via reloadType().
        std::unordered_map<types::IPPrefix, AreaRange> ranges; ///< Active area-range entries keyed by aggregate prefix.
        std::unordered_set<types::IPPrefix> rangePrefixes;     ///< Fast-lookup set of all configured range prefixes.
        std::atomic<bool> dcCompatible{true}; ///< True while all routers in the area support Demand Circuit operation (RFC 1793).

        Area& area;
    } priv;
};
} // namespace routing

#endif // OSPF_AREA_H

