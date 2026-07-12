/**
 * @file OspfInterface.h
 * @brief OSPF interface: state machine, neighbor management, and flooding.
 */

/**
 * @defgroup OSPF_INTERFACE OSPF Interface
 * @ingroup OSPF
 * @brief Per-interface state machine, interface manager, timers, and interface ID types.
 */

#ifndef OSPF_INTERFACE_H
#define OSPF_INTERFACE_H

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "ospf/FlagManager.hpp"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/interface/InterfaceTimers.h"
#include "InterfaceId.hpp"
#include "ospf/area/Area.h"

namespace interface { class Interface; }
namespace config { struct OspfRegistry; struct OspfAreaRegistry; }

namespace routing::ospf
{
class PacketDispatcher;
class OspfProcess;
class Topology;
class Area;
class Neighbor;
class LsdbTable;
struct LsaRecordRef;
struct LsaKey;
struct FloodInfo;
struct LsaHeader;

/**
 * @brief One router's candidacy in a DR/BDR election (RFC 2328 §9.4).
 * @ingroup OSPF_INTERFACE
 *
 * The election algorithm collects one of these for every eligible router on
 * the segment — the local router plus each neighbor in state 2-Way or higher
 * — populated from the values each router advertised in its most recent
 * Hello packet.  A router that lists itself as `claimedDr`/`claimedBdr` is
 * "declaring" for that role, which the two-pass election weighs above a mere
 * priority win so an established DR is not displaced by a newcomer
 * (RFC 2328 §9.4 step 3).
 */
struct DrCandidate
{
    uint32_t rid;        ///< Candidate's router ID; highest RID breaks priority ties.
    uint8_t priority;    ///< Advertised router priority; 0 makes the router ineligible for DR/BDR.
    uint32_t claimedDr;  ///< Router ID this candidate listed in the DR field of its Hello (self = declaring).
    uint32_t claimedBdr; ///< Router ID this candidate listed in the BDR field of its Hello (self = declaring).
};

/**
 * @brief Represents one OSPF-enabled interface within a process and area.
 * @ingroup OSPF_INTERFACE
 *
 * `OspfInterface` is the per-link protocol object. It owns:
 * - `NeighborTable` — the set of OSPF peers discovered on this segment.
 * - `InterfaceTimers` — Hello, Dead, retransmit, and pacing timer management.
 * - `InterfaceFlagManager` (×2) — per-interface event and LSA dirty flags.
 * - `PacketDispatcher*` — constructed during interface initialisation; handles
 *   version-specific packet encoding and multicast/unicast dispatch.
 *
 * The interface mirrors the RFC 2328 / RFC 5340 interface data structure and
 * drives the interface state machine (Down → Loopback / Waiting / P2P →
 * DROther / Backup / DR).
 *
 * ## Architectural Role
 * `OspfInterface` is the boundary between the link-layer hardware
 * (`interface::Interface`) and the OSPF protocol engine. Everything above it
 * (flooding, SPF, LSA origination) uses it as a handle for sending packets and
 * querying link parameters. Everything below it (socket, MTU, address) is
 * owned by `interface::Interface`.
 *
 * ## Lifecycle & Ownership
 * Created and stored by `InterfaceManager`. The constructor attaches the
 * interface to its area and allocates the appropriate `PacketDispatcher` (V2
 * or V3 depending on the process AF). The destructor stops all timers and
 * removes the interface from its area's interface list.
 *
 * ## Concurrency Model
 * `dr`, `bdr`, `isDr`, `isBdr`, `isVirtual`, `isMulticast`, `opaqueEnabled`,
 * and `isTransit` (on neighbors) are `std::atomic` because the data plane and
 * the SPF thread may read them concurrently with the control-plane thread that
 * updates them. All state-machine transitions go through the process scheduler.
 *
 * @warning The `area` reference must remain valid for the entire lifetime of
 * this object. `OspfInterface` must be destroyed before its owning `Area`.
 *
 * @see InterfaceManager
 * @see NeighborTable
 * @see InterfaceTimers
 */
class OspfInterface
{
    friend class ::Internal_OspfTest;
public:

    /**
     * @brief Constructs an OSPF interface and attaches it to its area.
     * @ingroup OSPF_INTERFACE
     *
     * Resolves the `Area` reference from the process, selects the correct
     * `PacketDispatcher` subclass (OSPFv2 or OSPFv3), and initialises cost
     * and timer values from @p configs.
     *
     * @param proc     The OSPF process that owns this interface.
     * @param iface    The underlying hardware/logical interface.
     * @param configs  Version-specific interface configuration registry.
     * @param id       Composite key (hardware index + area) for this interface.
     */
    OspfInterface(OspfProcess& proc, interface::Interface& iface,
                  const OspfInterfaceId& id);

    /**
     * @brief Destroys the OSPF interface.
     *
     * Stops the Hello timer, cancels all neighbor inactivity timers, tears
     * down any active adjacencies, and removes the interface from its area.
     */
    ~OspfInterface();

    /**
     * @brief Schedules `syncTimers()` on the process queue.
     *
     * Config-change entry point, called when the `hello-interval` or
     * `dead-interval` configuration is written.  The actual work — restarting
     * the Hello timer and rearming every in-flight neighbor inactivity timer
     * with the new dead interval — runs asynchronously on the scheduler
     * thread, so it is safe to call from the config-registry callback thread.
     */
    void enqueueSyncTimers();

    /**
     * @brief Schedules `syncNetworkType()` on the process queue.
     *
     * Config-change entry point, called when the `network` type
     * (broadcast / point-to-point / NBMA / point-to-multipoint) is
     * reconfigured.  The transition may trigger or suppress a DR/BDR
     * election and switch between multicast and unicast packet delivery,
     * all performed on the scheduler thread.
     */
    void enqueueSyncNetworkType();

    /**
     * @brief Schedules a reconciliation of statically configured neighbors on the process queue.
     *
     * Config-change entry point for NBMA/P2MP `neighbor <addr>` statements,
     * which name peers that cannot be discovered via multicast Hello.
     * Forwards to `NeighborTable::syncUnicast()`, which creates entries for
     * newly configured addresses and removes ones no longer configured.
     */
    void enqueueSyncUnicastNeighbors();

    /**
     * @brief Schedules a demand-circuit re-evaluation on the process queue.
     *
     * Config-change entry point for `demand-circuit` / `flood-reduction`
     * settings (RFC 1793).  Re-originates this interface's Router/Network
     * LSA contribution (the DC bit changes the advertised options) and
     * re-applies flood-reduction (DoNotAge) mode on the scheduler thread.
     */
    void enqueueSyncDemandCircuit();

    /**
     * @brief Schedules `syncDigestKey()` on the process queue.
     *
     * Config-change entry point for authentication configuration.  Reloads
     * the active key and key ID from the key-chain so subsequently sent
     * packets are signed with the new credentials; received-packet
     * verification picks up the same key.
     */
    void enqueueSyncDigestKey();

    /**
     * @brief Schedules an LSA re-origination for this interface on the process queue.
     *
     * Config-change entry point for `prefix-suppression`.  Recomputes this
     * interface's Router LSA contribution so its connected prefix is
     * advertised or withheld according to the new setting.
     */
    void enqueueSyncPrefixSuppression();

    Area& area;
    const OspfInterfaceId id;      ///< Immutable composite key for this interface.
    const uint32_t interfaceId;    ///< Hardware interface index, mirrored from id.interfaceId.
    const types::IPPrefix interfaceAddress; ///< Primary IP prefix (address + mask) assigned to this interface.
    interface::Interface& iface;   ///< Physical Interface that resides under this.
    const bool isVirtual = false;  ///< True for OSPFv3 virtual links.

    // GETTERS

    uint32_t getAreaId() const { return id.area; }
    uint16_t getCost() const { return priv.cost.load(std::memory_order_relaxed); }
    uint32_t getDrRid() const { return dr.rid.load(std::memory_order_acquire); }
    uint32_t getBdrRid() const { return bdr.rid.load(std::memory_order_acquire); }
    types::IPAddress getDrIp() const { return dr.ip.load(std::memory_order_relaxed); }
    types::IPAddress getBdrIp() const { return bdr.ip.load(std::memory_order_relaxed); }
    bool getIsDr() const { return priv.isDr.load(std::memory_order_acquire); }
    bool getIsBdr() const { return priv.isBdr.load(std::memory_order_acquire); }

private:

    friend class OspfProcess; 
    friend class PacketDispatcher;
    friend class InterfaceManager;
    friend class InterfaceTimers;
    friend class NeighborTable;
    friend class Neighbor;

    // INTERFACE

    /**
     * @brief Runs the DR/BDR election algorithm for this broadcast segment.
     *
     * Implements the two-pass election defined in RFC 2328 §9.4. Updates
     * `dr`, `bdr`, `isDr`, and `isBdr` and triggers any required LSA
     * re-origination when the election result changes.
     */
    void election();

    /**
     * @brief Recomputes the interface cost from the configured bandwidth or
     *        explicit cost override.
     */
    void calculateCost();

    /**
     * @brief Sets the DR router ID for this segment.
     *
     * @param dr  Router ID of the new DR (0 to clear).
     * @return True if the value changed and downstream state should be updated.
     */
    bool setDr(uint32_t dr);

    /**
     * @brief Sets the BDR router ID for this segment.
     *
     * @param bdr  Router ID of the new BDR (0 to clear).
     * @return True if the value changed and downstream state should be updated.
     */
    bool setBdr(uint32_t bdr);

    /**
     * @brief Propagates all configuration changes from the registry to live
     *        interface parameters.
     *
     * Called after any config write that affects this interface. Re-reads
     * cost, timers, authentication, network type, and passive mode.
     */
    void syncConfigs();

    /**
     * @brief Applies updated Hello and Dead interval values to the running timers.
     *
     * Restarts the Hello timer if the interval changed and rearms all
     * in-flight neighbor inactivity timers with the new dead interval.
     */
    void syncTimers();

    /**
     * @brief Reconciles the interface network type (broadcast, P2P, NBMA,
     *        P2MP) with the current configuration.
     *
     * May trigger a DR/BDR election or skip it depending on the new type.
     */
    void syncNetworkType();

    /**
     * @brief Loads the active cryptographic digest key for authentication.
     *
     * Reads the key-chain configuration and updates `authKey` and `authKeyId`.
     * Called on interface bring-up and whenever the key-chain changes.
     */
    void syncDigestKey();

    /**
     * @brief Configures flood-reduction mode for the specified interface.
     *
     * Sets the DoNotAge bit on all self-originated LSAs flooded out of `iface`
     * when flood reduction is enabled (RFC 2328 Appendix B).
     */
    void setFloodReduction();

    /**
     * @brief Requests re-origination of the LSAs this interface contributes to.
     *
     * Forwards to `IntraOriginator::updateInterface`, which rebuilds the
     * Router LSA link for this interface (transit / P2P / stub encoding may
     * all have changed) and originates or flushes the segment's Network LSA
     * if the local router is DR.  Called after state-machine transitions,
     * cost changes, and any config change that alters what this interface
     * advertises.
     */
    void updateOriginations();

    // NEIGHBOR

    /**
     * @brief Tears down and restarts all adjacencies on this interface.
     *
     * Forwards to `NeighborTable::resetNeighbors`: every neighbor is dropped
     * back to Down and rediscovered through the normal Hello exchange.  Used
     * during area and process resets, where stale adjacency state must not
     * survive the LSDB flush.
     */
    void resetNeighbors();

    /**
     * @brief Flushes all LSAs originated by the given neighbor from the area LSDB.
     *
     * Forwards to `Area::flushNeighborLsas` with the neighbor's router ID:
     * each matching LSA is set to MaxAge and flooded so the whole area learns
     * the withdrawal, then SPF is re-requested.  Called when a neighbor
     * falls out of FULL state and its topology information can no longer be
     * trusted.
     */
    void flushNeighborLsas(Neighbor& nbr);

    // AREA

    /**
     * @brief Checks whether an advertised LSA header is newer than the area's stored copy.
     *
     * Pass-through to `Area::compareLSASummary` (RFC 2328 §13.1 header
     * comparison).  Used during database exchange: for each header listed in
     * a received DD packet, a `true` result means the local copy is missing
     * or older and the LSA must be added to the Link State Request list.
     */
    bool compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const;

    /**
     * @brief Feeds a received LSA into the area's install/flood pipeline.
     *
     * Pass-through to `Area::processLsa`, which runs the full RFC 2328 §13
     * receive procedure: admission filtering by area type, newer/older
     * comparison, install, flood decision, and fight-back for stale
     * self-originated instances.  Exposed here so the packet dispatchers can
     * install LSAs through their interface without being friends of `Area`.
     *
     * @tparam Policy PolicyV2 or PolicyV3.
     * @return The area's install result, or std::nullopt if the LSA was
     *         silently dropped by the admission filter.
     */
    template <typename Policy>
    std::optional<Area::Result> processLsa(IncomingLsaContext& ctx, LsaBody& body);

    /**
     * @brief Triggers the area-wide Demand-Circuit compatibility re-scan.
     *
     * Pass-through to `Area::runDCIntegrityScan`, which checks whether every
     * router in the area still advertises DC capability (RFC 1793) and, on a
     * change, re-announces the DC bit and re-originates affected LSAs on all
     * demand-circuit interfaces.  Called after installing a Router or
     * Network LSA, since those carry the DC options bit.
     */
    void runAreaDCIntegrityScan();


    /**
     * @brief Atomic DR/BDR designation: router ID and IP address pair.
     *
     * Both fields are updated together during DR/BDR election. The IP address
     * is stored as a 128-bit value to support both IPv4 and IPv6 uniformly.
     */
    struct Designation { std::atomic<uint32_t> rid; std::atomic<__uint128_t> ip; };

    Designation dr;  ///< Current DR: router ID and interface IP.
    Designation bdr; ///< Current BDR: router ID and interface IP.

    std::atomic<bool> isMulticast = true;    ///< False on NBMA segments where unicast must be used.
    std::atomic<bool> opaqueEnabled = true;  ///< Whether opaque LSA capability is active on this interface.

    /**
     * @brief Demand-circuit negotiation state for this interface.
     *
     * RFC 1793 demand circuits suppress periodic Hellos once adjacency is
     * established. The state begins UNDECIDED and is resolved during the
     * Hello exchange with each neighbor.
     */
    enum class DcDecision { UNDECIDED, ENABLED, DISABLED };

    DcDecision demandCircuit = DcDecision::UNDECIDED; ///< Demand-circuit negotiation outcome.
    bool floodReduction = false;           ///< Whether flood reduction (RFC 2328 §G.2) is active.

    PacketDispatcher& dispatcher; ///< Version-specific packet dispatcher; allocated at construction.
    InterfaceTimers tmgr; ///< Time manager for this interface, manages delayed actions.
    NeighborTable ntable; ///< Neighbor table holding all neighbors that this interface manages.

    // NEIGHBOR ITERATION

    /**
     * @brief Invokes `fn(neighbor)` for every neighbor on this interface.
     *
     * Thin iteration wrapper over the neighbor table so collaborators
     * (timers, dispatchers, the state machine) can walk this interface's
     * neighbors without reaching into `ntable`'s storage directly.  `fn`
     * must not add or remove neighbors during iteration.
     */
    template <typename Fn>
    void forEachNeighbor(Fn&& fn);

    /**
     * @brief Const overload of @ref forEachNeighbor for read-only traversal.
     */
    template <typename Fn>
    void forEachNeighbor(Fn&& fn) const;

    InterfaceFlagManager flags; ///< Event flags (e.g. DR changed, neighbor state changed).
    InterfaceFlagManager lsaFlags; ///< LSA dirty flags driving re-origination decisions.

    OspfProcess& process;

    const config::OspfInterfaceBaseRegistry& baseConfigs; ///< Base (version-agnostic) interface config.
    const config::OspfInterfaceRegistry& configs;         ///< Version-specific interface config.

    // HELPERS

    const LsdbTable& getLsdb() const;
    const config::OspfRegistry& getProcessConfigs() const;
    const config::OspfAreaRegistry& getAreaConfigs() const;
    std::chrono::seconds getHelloInterval() const { return priv.helloTime; }
    std::chrono::seconds getDeadInterval() const { return priv.deadTime; }
    std::optional<__uint128_t> getAuthKey() const { return priv.authKey; }
    std::optional<uint8_t> getAuthKeyId() const { return priv.authKeyId; }

private:

    struct Private
    {
    private:
        friend class OspfInterface;
        friend class ::Internal_OspfTest;

        std::atomic<uint16_t> cost;            ///< Current interface cost in OSPF metric units.
        std::chrono::seconds helloTime;        ///< Configured Hello interval.
        std::chrono::seconds deadTime;         ///< Configured Dead interval (must be > helloTime).

        // AUTH
        std::optional<__uint128_t> authKey = std::nullopt;  ///< Active authentication key bytes; nullopt if no auth.
        std::optional<uint8_t> authKeyId = std::nullopt;    ///< Key ID associated with authKey.

        // DR / BDR
        std::atomic<bool> isDr = false;       ///< True when this router is the DR on this segment.
        std::atomic<bool> isBdr = false;      ///< True when this router is the BDR on this segment.
    } priv;
};
} // namespace routing::ospf

#endif // OSPF_INTERFACE_H
