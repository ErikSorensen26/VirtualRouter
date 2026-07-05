/**
 * @file Area.h
 * @brief OSPF area object: owns the LSDB, SPF manager, flood manager, and originator for one area.
 */

/**
 * @defgroup OSPF_AREA OSPF Area
 * @ingroup OSPF
 * @brief Area object, flood manager, flood queue/types, flag manager, and originator.
 */

#ifndef OSPF_AREA_H
#define OSPF_AREA_H

#include <memory_resource>

#include "configs/registry/router/OspfRegistry.h"
#include "ospf/database/LsdbTable.h"
#include "ospf/spf/SpfManager.h"
#include "FloodTypes.hpp"
#include "FlagManager.h"
#include "Originator.h"
#include "FloodManager.h"

namespace routing::ospf
{
struct OspfPath;
class OspfProcess;
class OspfInterface;

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
 * interfaces (@ref OspfInterface).  It is the unit of topology isolation in OSPF:
 * LSAs do not cross area boundaries except via ABR summary origination.
 *
 * ## Lifecycle & Ownership
 * Constructed and destroyed by OspfProcess.  The optional `mr` parameter lets
 * the owner supply a PMR arena (e.g., a pool resource) for the LSDB to avoid
 * per-record heap allocations.  Construction registers an aging timer;
 * destruction cancels all timers before the LSDB is torn down.
 *
 * ## Concurrency Model
 * All mutable Area state is accessed on the owning OspfProcess scheduler
 * thread.  The `options` and `dcCompatible` members are atomic because they
 * may be read from the Hello-send path which runs on the same scheduler but
 * can race with config-change callbacks.
 *
 * @warning `releaseMemory()` must only be called when the LSDB is quiescent
 * (no outstanding LsaRecordRef objects held by flood or retransmission queues).
 * Releasing the PMR pool while live references exist causes undefined behavior.
 *
 * @see OspfProcess, LsdbTable, SpfManager, FloodManager, Originator
 */
class Area
{
public:
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

    /**
     * @brief Constructs an OSPF area.
     * @ingroup OSPF_AREA
     *
     * Initializes the LSDB (using `mr` for storage if PMR is enabled), the
     * SPF manager, flood manager, and originator, then starts the LSA aging
     * timer.
     *
     * @param base  Owning OspfProcess.
     * @param area  32-bit area identifier (host byte order).
     * @param mr    Memory resource for LSDB storage; defaults to the global
     *              default_resource() if not supplied.
     */
    explicit Area(OspfProcess& base, uint32_t area, std::pmr::memory_resource* mr = std::pmr::get_default_resource());

    /**
     * @brief Destroys the area.
     *
     * Cancels all pending timers (aging, ignore, reset) before tearing down
     * the LSDB and flood manager to prevent timer callbacks from firing on
     * already-freed state.
     */
    ~Area();

    // Getters
    LsdbTable& lsdb() noexcept { return db; }
    const LsdbTable& lsdb() const noexcept { return db; }
    const OspfProcess& process() const noexcept { return base; }
    OspfProcess& process() { return base; }
    config::OspfAreaRegistry& getConfigs() { return configs; }
    const config::OspfAreaRegistry& getConfigs() const noexcept { return configs; }
    AreaFlagManager& getFlags() { return flags; }
    const AreaFlagManager& getFlags() const noexcept { return flags; }
    const SpfManager& getSpfManager() const noexcept { return spfMgr; }
    FloodManager& getFloodManager() noexcept { return floodMgr; }
    Originator& getOriginator() { return originator; }
    core::ProcessQueueRef& getScheduler() { return scheduler; }
    const core::ProcessQueueRef& getScheduler() const { return scheduler; }

    // FLOODING

    /**
     * @brief Transmits a batch of flood-ready LSAs out of the given interface.
     *
     * @param iface   Interface to send from.
     * @param records Batch of (FloodInfo, LsaRecordRef) pairs to transmit.
     */
    void send(OspfInterface& iface, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records);

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
     * @brief Determines the flood disposition for an already-installed LSA.
     *
     * Called after @ref processLsa to fill in the `reason` and flood signals
     * in `decision` based on context flags (self-originated, MaxAge, etc.).
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param[out] decision  Result struct to update with flood reasoning.
     * @param ctx       Context for the incoming LSA.
     */
    template <typename Policy>
    void evaluateDecision(Result& decision, const IncomingLsaContext& ctx);

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
    void suppressInterAreaPrefix(const types::IPPrefix& prefix) const;

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
     * @brief Prepares the area for a graceful reset without destroying it.
     *
     * Schedules a reset timer; the actual reset runs asynchronously so
     * in-flight LSA processing can complete first.
     */
    void initializeReset();

    /**
     * @brief Performs a full area reset: flushes all self-originated LSAs and clears neighbor state.
     *
     * After a reset the area re-originates its Router LSA and rejoins adjacencies
     * as if freshly started.
     */
    void reset();

    /**
     * @brief Clears the LSDB and all runtime state without destroying the area object.
     */
    void clear();

    /**
     * @brief Returns LSDB PMR pool memory to the upstream allocator.
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
     * @brief Configures flood-reduction mode for the specified interface.
     *
     * Sets the DoNotAge bit on all self-originated LSAs flooded out of `iface`
     * when flood reduction is enabled (RFC 2328 Appendix B).
     *
     * @param iface Interface on which flood reduction should be applied.
     */
    void setFloodReduction(OspfInterface& iface);

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
     * @brief Starts the periodic LSA aging timer.
     *
     * The timer fires every second and increments the age field of every LSA
     * in the LSDB.  LSAs reaching MaxAge are flushed via the flood path.
     */
    void startAgingTimer();

    /**
     * @brief Callback invoked on each aging timer tick to advance LSA ages.
     *
     * Ages all records in the LSDB by one second and enqueues MaxAge LSAs
     * for flushing.
     */
    void onAgingTick();

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

protected:
    std::pmr::memory_resource* mr{nullptr}; ///< PMR pool backing the LSDB; null means default allocator.

    std::atomic<uint8_t> options; ///< Area options byte advertised in Hello and DD packets; updated atomically.

    size_t ignoreSize{0};       ///< LSDB size threshold below which the ignore timer is suppressed.
    uint32_t ignoreTid{0};      ///< Timer handle for the LSA-ignore rate-limiting timer.
    uint32_t resetTid{0};       ///< Timer handle for the deferred area-reset timer.
    uint32_t agingTimerId{0};   ///< Timer handle for the one-second LSA aging tick.

    config::OspfAreaRegistry& configs; ///< Area-level OSPF configuration reference.

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

    std::unordered_map<types::IPPrefix, AreaRange> ranges; ///< Active area-range entries keyed by aggregate prefix.
    std::unordered_set<types::IPPrefix> rangePrefixes;     ///< Fast-lookup set of all configured range prefixes.

    LsdbTable db;          ///< Link-state database for this area.
    OspfProcess& base;     ///< Owning OSPF process.
    SpfManager spfMgr;     ///< Dijkstra SPF engine for this area.
    AreaFlagManager flags;  ///< Tracks area-type flags (stub, NSSA, etc.) and propagates changes.
    FloodManager floodMgr; ///< Manages reliable LSA flooding within this area.
private:
    bool onNewLsa();
    void ignoreLsa();
    void startIgnoreTimer();
    void startResetTimer();

    void installLsa(Result& result, const IncomingLsaContext& ctx, LsaBody& body);
    void installLsa(Result& result, const IncomingLsaContext& ctx, const LsaBody& body);

    Result process(IncomingLsaContext& ctx, const LsaBody& body);
    template <typename Policy>
    bool preProcess(IncomingLsaContext& ctx, const LsaBody& body);
    template <typename Policy>
    void postProcess(Result& result, IncomingLsaContext& ctx, const LsaBody& body);

    // Ranges
    std::unordered_map<types::IPPrefix, std::pair<uint32_t, uint32_t>> computeRangeContributors(const std::vector<std::pair<types::IPPrefix, OspfPath>>& intraAreaRoutes, const std::unordered_map<types::IPPrefix, AreaRange>& ranges);
    template <typename Policy>
    void applyRange(AreaRange& r);
    template <typename Policy>
    void withdrawRange(AreaRange& r);

    InstallResult evaluateIncomingLsa(const LsaRecord* existing, IncomingLsaContext& ctx, const LsaBody& body);
    LsaCompareResult compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const;
    bool compareLsaBody(const LsaBody& a, const LsaBody& b);

public:
    core::ProcessQueueRef scheduler; ///< Reference to the owning process scheduler; all area work is serialized through this.
    Originator& originator; ///< LSA originator shared with the owning process (Router LSA, Network LSA, etc.).

    const config::ospf::AreaType type;   ///< Area type (backbone, stub, NSSA, etc.); immutable after construction.
    const uint32_t areaId;               ///< 32-bit OSPF area identifier (host byte order); immutable after construction.

    std::atomic<bool> dcCompatible{true}; ///< True while all routers in the area support Demand Circuit operation (RFC 1793).
};
} // namespace routing

#endif // OSPF_LSA_FLOODING_ENGINE_H

