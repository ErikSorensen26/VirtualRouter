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

#include "ospf/interface/InterfaceManager.h"
#include "ospf/area/Area.h"
#include "ospf/topology/RoutingTable.h"
#include "ospf/topology/TopologyTable.h"
#include "ospf/abr/InterOriginator.h"
#include "ospf/asbr/ExternalOriginator.h"
#include "ospf/abr/InterRouteManager.h"
#include "ospf/asbr/ExternalRouteManager.h"
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
class OspfInterfaceBase;
class VirtualLink;
struct RouteManagerUtility;

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
    OspfV3Instance(config::Ospfv3AddressFamilyRegistry& cfgs)
        : configs(cfgs) {}

    OspfProcess* ipv4 = nullptr; ///< OSPFv3 process handling the IPv4 address family.
    OspfProcess* ipv6 = nullptr; ///< OSPFv3 process handling the IPv6 address family.

    config::Ospfv3AddressFamilyRegistry& configs; ///< Shared AF-level configuration reference.
};

/**
 * @brief Pairs IPv4 and IPv6 @ref OspfInterfaceBase pointers for a dual-stack interface binding.
 * @ingroup OSPF
 */
struct OspfInterfaceBaseInstance
{
    OspfInterfaceBase* IPv4; ///< Pointer to the IPv4 OSPF interface instance.
    OspfInterfaceBase* IPv6; ///< Pointer to the IPv6 OSPF interface instance.
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
 * - An internal @ref core::ProcessQueue (`scheduler`) that serializes all
 *   LSA processing, SPF runs, and origination work.
 * - The process-scoped role objects: @ref InterOriginator /
 *   @ref InterRouteManager (ABR) and @ref ExternalOriginator /
 *   @ref ExternalRouteManager (ASBR, which also owns the process-wide
 *   external LSDB).
 *
 * ## Architectural Role
 * OspfProcess sits between the VirtualRouter (which owns the process) and the
 * per-area machinery (Area, IntraOriginator, FloodManager).  It is the sole
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
 * All mutable state (areas, rib, the external LSDB) is accessed exclusively
 * on the scheduler thread.  The only exceptions are the `isV3` flag
 * (immutable) and configuration reads via `configs`, which are lock-free by
 * design.
 *
 * @warning Do not access the areas, the external LSDB, or `rib` from outside
 * the scheduler thread.  Doing so races with ongoing SPF computation or LSA
 * flooding and will silently corrupt routing state.
 *
 * @see Area, InterfaceManager, OspfRib, InterOriginator, ExternalOriginator
 */
class OspfProcess
{
    friend ::Internal_OspfTest;
public:

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
    OspfProcess(bool isV3, uint16_t procId, types::AddressFamily af, core::VirtualRouter& vrf);

    /**
     * @brief Destroys the OSPF process.
     *
     * Cancels all timer subscriptions, drains the scheduler, tears down all
     * areas and interfaces, and unregisters interface-event listeners from the
     * owning VRF.
     */
    ~OspfProcess();

    /**
     * @brief Schedules a graceful process reset.
     *
     * Clears all neighbor adjacencies, flushes self-originated LSAs, and
     * re-initializes all areas.  Useful after a configuration change that
     * requires a full re-convergence (e.g., Router ID change).
     */
    void enqueueReset();

    /**
     * @brief Begins graceful restart (RFC 3623) on every owned interface.
     *
     * Originates a Grace-LSA on each interface and marks the process as
     * restarting so outgoing Hellos carry the LLS restart bit. Deliberately
     * does *not* call @ref enqueueReset — graceful restart's entire purpose
     * is to preserve existing neighbor adjacencies and LSDB state across the
     * restart, not to tear them down.
     *
     * @param gracePeriodSeconds Grace period advertised to neighbors.
     * @param reason Restart reason advertised in the Grace-LSA.
     */
    void beginGracefulRestart(uint32_t gracePeriodSeconds, GraceRestartReason reason);

    /**
     * @brief Ends graceful restart on every owned interface.
     *
     * Flushes each interface's Grace-LSA and clears the in-progress flag.
     */
    void endGracefulRestart();

    /**
     * @brief Schedules a neighbor-configuration sync on the process queue.
     *
     * Config-change entry point: re-applies static `neighbor` configuration
     * across all OSPF interfaces.
     */
    void enqueueSyncNeighbor();

    /**
     * @brief Schedules an interface-list refresh on the process queue.
     *
     * Config-change entry point: re-evaluates which VRF interfaces should be
     * running OSPF (e.g. after `network` statements change).
     */
    void enqueueSyncNetworks();

    /**
     * @brief Schedules a `summary-address` configuration sync on the process queue.
     *
     * Config-change entry point: delegates to
     * `ExternalOriginator::syncSummaryConfig()` on the scheduler thread.
     */
    void enqueueSyncSummaries();

    bool isASBR() const; ///< Returns true if this process is currently acting as an ASBR.
    bool isABR() const;  ///< Returns true if this process is currently acting as an ABR.
    uint32_t getRouterId() const;

    const bool isV3; ///< True when this process uses OSPFv3 packet encoding (RFC 5340).
    const bool afCapable = false;
    const uint16_t procId; ///< Process identifier; immutable after construction.
    const types::AddressFamily af; ///< Address family this process services.
    core::VirtualRouter& routingInstance; ///< Owning VRF; used to query interface and RIB state.

private:
    friend class Area;
    friend class OspfRib;
    friend class InterfaceManager;
    friend class OspfInterfaceBase;
    friend class OspfInterface;
    friend class VirtualLink;
    friend class InterOriginator;
    friend class ExternalOriginator;
    friend struct RouteManagerUtility;

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
     * @brief Looks up an area origin context by its 32-bit area ID.
     *
     * @param areaId OSPF area identifier in host byte order.
     * @return Pointer to the @ref OriginatorContext, or nullptr if not found.
     */
    OriginatorContext* getOriginCtx(uint32_t areaId);

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

    /**
     * @brief Returns the number of areas currently configured on this process.
     */
    size_t areaSize() const;

    /**
     * @brief Invokes `fn(areaId, originContext)` for every area on this process.
     *
     * Lets the process-scoped originators reach each area's
     * @ref OriginatorContext without exposing the area map itself.
     */
    template <typename Fn>
    void forEachOriginCtx(Fn&& fn);

    /**
     * @brief Const overload of @ref forEachOriginCtx.
     */
    template <typename Fn>
    void forEachOriginCtx(Fn&& fn) const;

    /**
     * @brief Invokes `fn(areaId, area)` for every area on this process.
     *
     * Lets `InterfaceManager` enumerate each area's `virtual-link`
     * configuration list without exposing the area map itself.
     */
    template <typename Fn>
    void forEachArea(Fn&& fn);

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

    OspfRib rib; ///< SPF-computed routes awaiting installation into the global RIB.
    TopologyTable table; ///< Process-wide topology table used for SPF and route computation.
    core::ProcessQueue scheduler; ///< Serialization queue for all LSA/SPF/origination work.

    InterOriginator interOriginator;       ///< ABR role: inter-area summary / ASBR-reachability origination.
    ExternalOriginator externalOriginator; ///< ASBR role: external LSA origination; owns the process-wide external LSDB.

    InterRouteManager interRouteManager;       ///< Derives inter-area routes from Type-3/Type-4 LSAs.
    ExternalRouteManager externalRouteManager; ///< Derives external routes from the process-wide external LSDB.

    InterfaceManager ifaceMgr; ///< Manages all OSPF-enabled interfaces in the owning VRF.

    // CONFIGS
    const config::OspfRegistry& configs; ///< Process-level OSPF configuration reference.
private:
    /**
     * @brief State hidden even from this class's friends.
     *
     * `OspfProcess` grants friendship to many internal collaborators; members
     * that no collaborator should touch directly (the area map, role flags,
     * event-subscription handles) live here, where only `OspfProcess` itself
     * has access.
     */
    struct Private
    {
    private:
        friend class OspfProcess;

        Private(OspfProcess& process);

        OspfProcess& process;

        // AREAS
        std::unordered_map<uint32_t, Area> areas; ///< All OSPF areas for this process, keyed by area ID.

        // ROUTER ROLE FLAGS
        bool abr = false;  ///< True when this router is an ABR (connects backbone to non-backbone area).
        bool asbr = false; ///< True when this router is an ASBR (redistributes external routes).
        std::atomic<uint32_t> rid;      ///< Elected or configured Router ID (host byte order).

        // INTERFACE EVENT SUBSCRIPTIONS
        uint32_t ifUpId, ifDownId, ipReadyId, ipDelId; ///< Event subscription handles; used to deregister on destruction.
    } priv;
};

template <typename Fn>
void OspfProcess::forEachOriginCtx(Fn&& fn)
{
    for (auto& [id, area] : priv.areas)
        fn(id, area.originContext);
}

template <typename Fn>
void OspfProcess::forEachArea(Fn&& fn)
{
    for (auto& [id, area] : priv.areas)
        fn(id, area);
}

template <typename Fn>
void OspfProcess::forEachOriginCtx(Fn&& fn) const
{
    for (const auto& [id, area] : priv.areas)
        fn(id, area.originContext);
}
} // namespace routing

#endif // OSPF_H

