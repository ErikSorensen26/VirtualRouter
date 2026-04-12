/**
 * @file OspfProcess.h
 * @brief Core OSPF process object owning areas, interfaces, RIB, and scheduler.
 */

/**
 * @defgroup OSPF OSPF
 * @ingroup ROUTING
 * @brief OSPFv2 (RFC 2328) and OSPFv3 (RFC 5340) dual-stack implementation.
 */

#ifndef OSPF_H
#define OSPF_H

#include <ControlScheduler.h>

#include "interface/InterfaceManager.h"
#include "ospf/interface/InterfaceManager.h"
#include "ospf/area/Area.h"
#include "ospf/topology/RoutingTable.h"
#include "ospf/topology/TopologyTable.h"
#include "configs/registry/router/OspfRegistry.h"

namespace core { class VirtualRouter; }

/**
 * @namespace routing::ospf
 * @brief OSPFv2/v3 protocol implementation: processes, areas, interfaces, neighbors, LSDB, and SPF.
 *
 * The top-level entry point is @ref OspfProcess.  OSPFv3 dual-stack operation
 * is coordinated through @ref OspfV3Instance, which owns one OspfProcess per
 * address family.  All SPF and LSA work is serialized through each process's
 * internal scheduler (@ref core::ProcessQueue).
 */
namespace routing::ospf
{
class Topology;
class OspfProcess;
class OspfInterface;
class Area;

/**
 * @brief Container that pairs the IPv4 and IPv6 @ref OspfProcess instances for an OSPFv3 deployment.
 * @ingroup OSPF
 *
 * OSPFv3 is address-family-aware: the same protocol machinery runs separately
 * for IPv4 (AF=IPv4, using OSPFv3 encoding) and IPv6.  This struct holds
 * both halves under a shared address-family configuration reference.
 *
 * ## Lifecycle & Ownership
 * Owned by the VirtualRouter.  Each OspfProcess pointer is non-owning; the
 * actual OspfProcess objects are created and destroyed by the router framework.
 */
struct OspfV3Instance
{
    /**
     * @brief Constructs an OspfV3Instance with the given address-family configuration reference.
     *
     * The `ipv4` and `ipv6` process pointers are left null and must be assigned
     * after the OspfProcess objects are created by the router framework.
     *
     * @param cfgs Shared OSPFv3 address-family configuration reference.
     */
    OspfV3Instance(config::OspfAddressFamilyV3Registry& cfgs)
        : configs(cfgs) {}

    OspfProcess* ipv4 = nullptr; ///< OSPFv3 process handling the IPv4 address family.
    OspfProcess* ipv6 = nullptr; ///< OSPFv3 process handling the IPv6 address family.

    config::OspfAddressFamilyV3Registry& configs; ///< Shared AF-level configuration reference.
};

/**
 * @brief Pairs IPv4 and IPv6 @ref OspfInterface pointers for a dual-stack interface binding.
 * @ingroup OSPF
 */
struct OspfInterfaceInstance
{
    OspfInterface* IPv4; ///< Pointer to the IPv4 OSPF interface instance.
    OspfInterface* IPv6; ///< Pointer to the IPv6 OSPF interface instance.
};

/**
 * @brief Single OSPF routing process owning one set of areas, interfaces, RIB, and scheduler.
 * @ingroup OSPF
 *
 * An OspfProcess models the RFC 2328 / RFC 5340 concept of an OSPF routing
 * process running inside a VRF.  One process may cover many areas.  The
 * process is parameterized by address family and protocol version at runtime
 * (`isV3`) and by policy type at the template call sites (PolicyV2 / PolicyV3).
 *
 * Each instance contains:
 * - An @ref InterfaceManager tracking all OSPF-enabled interfaces in the VRF.
 * - A map of @ref Area objects, one per configured OSPF area.
 * - An @ref OspfRib holding SPF-computed routes before they are installed into
 *   the global routing table.
 * - An internal @ref core::ProcessQueue (`scheduler`) that serializes all LSA
 *   processing, SPF runs, and origination work.
 * - An external LSDB (`externalDb`) for AS-External and NSSA-External LSAs,
 *   which are scoped to the process rather than a single area.
 *
 * ## Architectural Role
 * OspfProcess sits between the VirtualRouter (which owns the process) and the
 * per-area machinery (Area, Originator, FloodManager).  It is the sole
 * authority for router-wide state: ABR/ASBR flags, summary origination across
 * areas, and redistribution of external routes into OSPF.
 *
 * ## Lifecycle & Ownership
 * Created and destroyed by the VirtualRouter framework.  Construction
 * subscribes to interface-up/down and IP-ready/deleted events so the process
 * reacts to hardware changes without polling.  Destruction must drain the
 * scheduler before the Area or InterfaceManager destructors run.
 *
 * ## Concurrency Model
 * All mutable state (areas, rib, externalDb) is accessed exclusively on the
 * scheduler thread.  The only exceptions are the atomic `isV3` flag and
 * configuration reads via `configs`, which are lock-free by design.
 *
 * @warning Do not access `areas`, `externalDb`, or `rib` from outside the
 * scheduler thread.  Doing so races with ongoing SPF computation or LSA
 * flooding and will silently corrupt routing state.
 *
 * @see Area, InterfaceManager, OspfRib
 */
class OspfProcess
{
public:
    using V3AfConfigs = config::OspfAddressFamilyV3Registry; ///< OSPFv3 AF config reference type alias.
    using V2AfConfigs = config::OspfAddressFamilyV2Registry; ///< OSPFv2 AF config reference type alias.

    /**
     * @brief Constructs an OSPF process.
     * @ingroup OSPF
     *
     * Subscribes to interface-up, interface-down, IP-ready, and IP-deleted
     * events on the owning VRF so the process automatically brings interfaces
     * in and out of OSPF as the hardware state changes.
     *
     * @param isV3  True if this process runs OSPFv3 packet encoding (RFC 5340).
     * @param procId Process identifier (shown in show commands and used as config key).
     * @param af    Address family this process services (IPv4 or IPv6).
     * @param vrf   Owning VirtualRouter; must outlive this process.
     */
    OspfProcess(bool isV3, uint16_t procId, types::AddressFamily af, core::VirtualRouter* vrf);

    /**
     * @brief Destroys the OSPF process.
     *
     * Cancels all timer subscriptions, drains the scheduler, tears down all
     * areas and interfaces, and unregisters interface-event listeners from the
     * owning VRF.
     */
    ~OspfProcess();

    // EXTERNAL ORIGINATION

    /**
     * @brief Distributes a received external LSA to all other areas (NSSA translation path).
     *
     * Called when an ABR receives an NSSA-External LSA and must translate and
     * flood it as an AS-External LSA into the non-NSSA areas of this process.
     *
     * @tparam Policy  PolicyV2 or PolicyV3 — selects LSA wire format.
     * @param sourceArea Area that received the original NSSA LSA.
     * @param ctx        Context for the incoming LSA (key, header, flood info).
     * @param body       Decoded LSA body to distribute.
     */
    template <typename Policy>
    void distributeExternalLsa(const Area& sourceArea, IncomingLsaContext& ctx, const LsaBody& body);

    /**
     * @brief Originates or withdraws a single AS-External (or NSSA-External) LSA.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctx    Origination context describing the redistributed route.
     * @param expire True to flush (MaxAge) the LSA rather than originate it.
     */
    template <typename Policy>
    void originateExternal(ExternalOriginateContext& ctx, bool expire);

    /**
     * @brief Batch version of @ref originateExternal for multiple routes.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param ctxs Vector of (context, expire) pairs to process in one shot.
     */
    template <typename Policy>
    void originateExternals(std::vector<std::pair<ExternalOriginateContext, bool>>& ctxs);

    /**
     * @brief Constructs the @ref LsaKey for a redistributed external route.
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param ctx       Origination context.
     * @param isNssa    True when building a key for an NSSA-External LSA.
     * @return          The LsaKey that uniquely identifies this LSA in the LSDB.
     */
    template <typename Policy>
    LsaKey buildExternalKey(ExternalOriginateContext& ctx, bool isNssa);

    /**
     * @brief Fills in the LSA body fields for a redistributed external route.
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param ctx       Origination context carrying metric, tag, and forwarding address.
     * @param[out] body External LSA body to populate.
     * @param isNssa    True when building an NSSA-External LSA body.
     */
    template <typename Policy>
    void buildExternalBody(ExternalOriginateContext& ctx, Policy::ExternalLsa& body, bool isNssa);

    // SUMMARY ORIGINATION

    /**
     * @brief Re-originates inter-area summary LSAs for a list of route changes.
     *
     * Called after an SPF run when intra-area routes change.  For each changed
     * route this process is an ABR for, it re-computes and floods a Type-3
     * (OSPFv2) or Inter-Area-Prefix (OSPFv3) summary LSA into adjacent areas.
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param sourceArea Area whose SPF results changed.
     * @param pathList   List of route additions/withdrawals from that SPF run.
     */
    template <typename Policy>
    void reoriginateSummaries(Area& sourceArea, std::vector<OspfRouteChange>& pathList);

    /**
     * @brief Re-originates a single inter-area summary LSA for one route change.
     *
     * @tparam Policy   PolicyV2 or PolicyV3.
     * @param sourceArea Area whose route changed.
     * @param path       The individual route change to summarize.
     */
    template <typename Policy>
    void reoriginateSummary(Area& sourceArea, OspfRouteChange& path);

    /**
     * @brief Synchronizes the summary-address configuration with the active origination state.
     *
     * Reads the current `summary-address` CLI configuration and reconciles it
     * against the running set of summary LSAs, triggering re-origination or
     * withdrawal as needed.
     */
    void syncSummaryConfig();

    // Getters
    types::AddressFamily getAF() { return af; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    const InterfaceManager& getIfaceMgr() const noexcept { return ifaceMgr; }
    config::OspfRegistry& getConfigs() { return configs; }
    const config::OspfRegistry& getConfigs() const noexcept { return configs; }
    OspfRib& getRib() { return rib; }
    core::ProcessQueueRef getScheduler() { return scheduler.ref(); }
    const OspfRib& getRib() const { return rib; }
    uint16_t getProcId() const { return procId; }
    uint32_t getRouterId() const
    {
        const auto& id = configs.reg.get<config::Ospf::ROUTER_ID>();
        if (id.hasValue()) return id.load();
        return rid;
    }

    /**
     * @brief Selects and stores the Router ID for this process.
     *
     * Follows the standard OSPF election order:
     * 1. Configured `router-id` value (takes priority).
     * 2. Highest IPv4 address on a loopback interface in the VRF.
     * 3. Highest IPv4 address on any non-loopback interface.
     *
     * @return True if a Router ID was found, false if no IPv4 address is available.
     *
     * @note This is called once at process startup.  The RID does not change
     * dynamically as interface addresses are added or removed after the process
     * has started advertising Hello packets.
     */
    bool calculateRID();

    // AREAS

    /**
     * @brief Looks up an area by its 32-bit area ID.
     *
     * @param areaId OSPF area identifier in host byte order.
     * @return Pointer to the @ref Area, or nullptr if not found.
     */
    Area* getArea(uint32_t areaId);

    /**
     * @brief Returns an existing area or creates a new one if it does not exist.
     *
     * @param areaId OSPF area identifier in host byte order.
     * @return Reference to the (possibly newly created) @ref Area.
     */
    Area& insureArea(uint32_t areaId);

    /**
     * @brief Removes an area and recalculates ABR status.
     *
     * Erases the area from the process map and re-evaluates whether this router
     * is still an ABR. Called automatically when the last interface in a
     * non-backbone area is removed.
     *
     * @param areaId OSPF area identifier to remove.
     */
    void removeArea(uint32_t areaId);

    // ROUTER TYPE FLAGS

    /**
     * @brief Sets or clears the ASBR (Autonomous System Boundary Router) flag.
     *
     * When set to true the process re-originates its Router LSA with the ASBR
     * bit set and starts accepting redistributed external routes.
     *
     * @param val True to mark this router as an ASBR.
     */
    void setASBR(bool val);

    /**
     * @brief Sets or clears the ABR (Area Border Router) flag.
     *
     * When set to true the process activates inter-area summary origination.
     *
     * @param val True to mark this router as an ABR.
     */
    void setABR(bool val);
    bool isASBR(); ///< Returns true if this process is currently acting as an ASBR.
    bool isABR();  ///< Returns true if this process is currently acting as an ABR.

    /**
     * @brief Schedules a graceful process reset.
     *
     * Clears all neighbor adjacencies, flushes self-originated LSAs, and
     * re-initializes all areas.  Useful after a configuration change that
     * requires a full re-convergence (e.g., Router ID change).
     */
    void initiateReset();

    /**
     * @brief Advertises or withdraws the OSPF default route.
     *
     * When `add` is true, originates a Type-5 (or Type-7 into NSSA areas)
     * LSA for 0.0.0.0/0 with the configured metric and metric type.
     *
     * @param add True to inject the default route, false to withdraw it.
     */
    void addDefaultRoute(bool add);

    const bool isV3; ///< True when this process uses OSPFv3 packet encoding (RFC 5340).
    const bool afCapable = false;

    core::VirtualRouter* routingInstance = nullptr; ///< Owning VRF; used to query interface and RIB state.

    // EXTERNAL LSDB (process-scoped)
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb; ///< AS-External and NSSA-External LSAs, keyed by LsaKey.
    std::atomic<uint32_t> monotonicExternalId{0}; ///< Monotonically increasing Link-State ID allocator for external LSAs.

    // SUMMARY LSA TRACKING
    std::unordered_map<types::IPPrefix, uint32_t> intraLsids; ///< Maps intra-area prefix to its allocated Link-State ID for summary LSAs.
    std::atomic<uint32_t> monotonicIntraId{0}; ///< Monotonically increasing Link-State ID allocator for intra-area prefix summaries.

    TopologyTable table; ///< Process-wide topology table used for SPF and route computation.

private:
    /**
     * @brief Runtime state for a configured `summary-address` aggregation prefix.
     *
     * Tracks both the operator-supplied configuration and the live contribution
     * count derived from intra-area routes covered by the range.  When
     * `contributorCount` drops to zero the aggregate LSA is withdrawn.
     */
    struct OspfSummaryAddress {
        // Config
        bool notAdvertise = false;          ///< If true the summary is suppressed (black-hole only).
        bool nssaOnly = false;              ///< If true limit this summary to NSSA areas.
        std::optional<uint32_t> tag;        ///< Optional route tag to stamp on the originated LSA.

        // Runtime
        uint32_t contributorCount = 0;      ///< Number of more-specific routes currently covered.
        uint32_t computedMetric = 0;        ///< Best metric among contributing routes.
        bool isType2;                       ///< True if the aggregate uses a Type-2 (E2) metric.

        uint32_t lsId;                      ///< Allocated Link-State ID for the originated LSA.
        bool discardPresent = false;        ///< True if a discard (null-route) has been installed in the RIB.
    };

    // SUMMARIES
    std::unordered_map<types::IPPrefix, OspfSummaryAddress> summaries; ///< Active summary-address entries keyed by aggregate prefix.

    /**
     * @brief Suppresses or unsuppresses more-specific external LSAs covered by a summary.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @param activeSummaries The set of summary prefixes currently in effect.
     */
    template <typename Policy>
    void syncSummarySuppression(std::unordered_map<types::IPPrefix, OspfSummaryAddress>& activeSummaries);

    // AREAS
    std::unordered_map<uint32_t, Area> areas; ///< All OSPF areas for this process, keyed by area ID.

    OspfRib rib;              ///< SPF-computed routes awaiting installation into the global RIB.
    core::ProcessQueue scheduler; ///< Serialization queue for all LSA/SPF/origination work.

    // ROUTER ROLE FLAGS
    bool abr = false;  ///< True when this router is an ABR (connects backbone to non-backbone area).
    bool asbr = false; ///< True when this router is an ASBR (redistributes external routes).
    uint32_t rid;      ///< Elected or configured Router ID (host byte order).

    std::optional<uint32_t> defaultRoute = std::nullopt; ///< Link-State ID of the originated default-route LSA, if any.

    const uint16_t procId; ///< Process identifier; immutable after construction.
    const types::AddressFamily af; ///< Address family this process services.
    InterfaceManager ifaceMgr; ///< Manages all OSPF-enabled interfaces in the owning VRF.

    // INTERFACE EVENT SUBSCRIPTIONS
    uint32_t ifUpId, ifDownId, ipReadyId, ipDelId; ///< Event subscription handles; used to deregister on destruction.

    // CONFIGS
    std::variant<std::monostate, std::reference_wrapper<V3AfConfigs>, std::reference_wrapper<V2AfConfigs>> afConfigs; ///< Address-family-specific config reference (v2 or v3).
    config::OspfRegistry& configs; ///< Process-level OSPF configuration reference.
};
} // namespace routing

#endif // OSPF_H

