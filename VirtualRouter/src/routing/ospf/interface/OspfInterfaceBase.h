/**
 * @file OspfInterfaceBaseBase.h
 * @brief OSPF interface: state machine, neighbor management, and flooding.
 */

/**
 * @defgroup OSPF_INTERFACE OSPF Interface
 * @ingroup OSPF
 * @brief Per-interface state machine, interface manager, timers, and interface ID types.
 */

#ifndef OSPF_INTERFACE_BASE_H
#define OSPF_INTERFACE_BASE_H

#include "ospf/FlagManager.hpp"
#include "ospf/interface/InterfaceTimers.h"
#include "ospf/interface/GracefulRestartManager.h"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/ospfv2/database/GraceLsa.hpp"
#include "ospf/area/Area.h"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "InterfaceId.hpp"
#include "ControlScheduler.h"

namespace interface { class Interface; }
namespace config { struct OspfRegistry; struct OspfAreaRegistry; }
namespace types { struct IPAddress; }

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
 * @brief Represents one OSPF-enabled interface within a process and area.
 * @ingroup OSPF_INTERFACE
 *
 * `OspfInterfaceBase` is the per-link protocol object. It owns:
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
 * `OspfInterfaceBase` is the boundary between the link-layer hardware
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
 * this object. `OspfInterfaceBase` must be destroyed before its owning `Area`.
 *
 * @see InterfaceManager
 * @see NeighborTable
 * @see InterfaceTimers
 */
class OspfInterfaceBase
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
     * @param proc      The OSPF process that owns this interface.
     * @param id        Composite key (hardware index + area) for this interface.
     */
    template <typename T>
    OspfInterfaceBase(OspfProcess& proc, const OspfInterfaceId& id, const T& configs);

    /**
     * @brief Destroys the OSPF interface.
     *
     * Stops the Hello timer, cancels all neighbor inactivity timers, tears
     * down any active adjacencies, and removes the interface from its area.
     */
    ~OspfInterfaceBase();

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

    Area& area;
    const OspfInterfaceId id;   ///< Immutable composite key for this interface.
    const uint32_t interfaceId; ///< Hardware interface index, mirrored from id.interfaceId.
    OspfProcess& process;       ///< Owning process; must be initialized before `dispatcher`, which reads `getProcessConfigs()` during construction.

    // GETTERS

    uint32_t getAreaId() const { return id.area; }
    bool getOpaqueEnabled() const { return opaqueEnabled.load(std::memory_order_relaxed); }
    bool getGracefulRestartInProgress() const { return gracefulRestartInProgress.load(std::memory_order_relaxed); }

    /**
     * @brief True for `VirtualLink` instances, false for `OspfInterface`.
     *
     * Used by `IntraOriginator::addRouterLink` to choose Type-4 virtual-link
     * encoding over Type-1 point-to-point encoding for an otherwise-identical
     * FULL P2P adjacency.
     */
    virtual bool isVirtualLink() const { return false; }

    /**
     * @brief Whether Hello packets should be sent multicast on this interface.
     *
     * `OspfInterface` reports whether the current network type uses the
     * AllSPFRouters/AllDRouters multicast groups. Virtual links never have a
     * shared-media multicast domain and always send unicast Hellos.
     */
    virtual bool getIsMulticast() const { return false; }

    /**
     * @brief Router-LSA link metric for this interface.
     *
     * `OspfInterface` returns its configured/computed interface cost.
     * `OspfVirtualLink` returns the transit area's current intra-area SPF
     * cost to the remote endpoint (RFC 2328 SS15), 0 if unresolved.
     */
    virtual uint16_t getCost() const = 0;

    /**
     * @brief Effective network type for protocol purposes.
     *
     * `OspfInterface` reads the configured `network` type; `OspfVirtualLink`
     * always reports POINT_TO_POINT (RFC 2328 SS15 -- virtual links behave as
     * unnumbered point-to-point links).
     */
    virtual config::ospf::NetworkType getNetworkType() const = 0;

    /**
     * @brief Whether this interface is administratively passive.
     *
     * Virtual links are never passive: they always run the Hello protocol
     * with the configured virtual neighbor.
     */
    virtual bool getPassive() const = 0;

    /**
     * @brief Router priority advertised in Hello packets.
     *
     * Meaningless off a virtual link (no DR/BDR election); `OspfVirtualLink`
     * returns 0.
     */
    virtual uint8_t getPriority() const = 0;

    /**
     * @brief Whether MTU mismatches should be ignored during DD exchange.
     *
     * Virtual links have no real MTU to compare (RFC 2328 SS15) and always
     * ignore it.
     */
    virtual bool getMtuIgnore() const = 0;

    /**
     * @brief Whether outbound LSU flooding should be filtered on this interface.
     *
     * Virtual links have no `database-filter` configuration and never filter.
     */
    virtual bool getDatabaseFilter() const = 0;

    /**
     * @brief Whether Demand-Circuit auto-negotiation should be skipped.
     *
     * Virtual links never negotiate demand circuits, so this is irrelevant
     * to them; `OspfInterface` reads its configured `demand-circuit ignore`.
     */
    virtual bool getDemandCircuitIgnore() const = 0;

    /**
     * TODO add doxy comment
     */
    virtual bool getLls() const = 0;

    /**
     * @brief Resolves the hardware interface packets should currently be
     *        transmitted on.
     *
     * `OspfInterface` always returns its own bound `iface`. `OspfVirtualLink`
     * has no fixed hardware interface: it resolves the local egress interface
     * dynamically from the transit area's SPF result each time it is asked
     * (RFC 2328 SS15), and returns `nullptr` if no intra-area path to the
     * remote endpoint currently exists (the send is dropped; the next Hello
     * retry will re-resolve once SPF reconverges).
     */
    virtual interface::Interface* getTransmitInterface() const = 0;
    
    /**
     * TODO add doxy comment
     */
    virtual const types::IPPrefix& getTransmitAddress() const = 0;

    // GRACEFUL RESTART (RFC 3623)

    /**
     * @brief Begins graceful restart signaling on this interface: originates
     * a Grace-LSA and sets the LLS restart bit on subsequent Hellos.
     *
     * @param gracePeriodSeconds Grace period advertised to neighbors.
     * @param reason Restart reason code (RFC 3623 SS3).
     */
    void beginGracefulRestart(uint32_t gracePeriodSeconds, GraceRestartReason reason);

    /**
     * @brief Ends graceful restart signaling on this interface: flushes the
     * Grace-LSA and clears the LLS restart bit.
     */
    void endGracefulRestart();

    /**
     * @brief Schedules `syncDigestKey()` on the process queue.
     *
     * Config-change entry point for authentication configuration (base/global
     * config, so this applies to virtual links as well as regular
     * interfaces).  Reloads the active key and key ID from the key-chain so
     * subsequently sent packets are signed with the new credentials;
     * received-packet verification picks up the same key.
     */
    void enqueueSyncDigestKey();

protected:

    friend class OspfProcess; 
    friend class PacketDispatcher;
    friend class InterfaceManager;
    friend class InterfaceTimers;
    friend class NeighborTable;
    friend class Neighbor;

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
     * @brief Loads the active cryptographic digest key for authentication.
     *
     * Reads the key-chain configuration and updates `authKey` and `authKeyId`.
     * Called on interface bring-up and whenever the key-chain changes.
     */
    void syncDigestKey();

    /**
     * @brief Demand-circuit negotiation state for this interface.
     *
     * RFC 1793 demand circuits suppress periodic Hellos once adjacency is
     * established. The state begins UNDECIDED and is resolved during the
     * Hello exchange with each neighbor.
     */
    enum class DcDecision { UNDECIDED, ENABLED, DISABLED };

    DcDecision demandCircuit = DcDecision::UNDECIDED; ///< Demand-circuit negotiation outcome.
    bool floodReduction = false; ///< Whether flood reduction (RFC 2328 §G.2) is active.

    /**
     * @brief Runs the DR/BDR election algorithm, if applicable.
     *
     * No-op on `OspfInterfaceBase`: virtual links (RFC 2328 SS15) are always
     * point-to-point and never elect a DR/BDR. `OspfInterface` overrides this
     * with the RFC 2328 SS9.4 two-pass election.
     */
    virtual void election() {}

    /**
     * @brief Recomputes flood-reduction (DoNotAge) state from Demand-Circuit config.
     *
     * No-op on `OspfInterfaceBase`: virtual links never negotiate demand
     * circuits (see `getDemandCircuitIgnore()`) and never enable flood
     * reduction. `OspfInterface` overrides this to read its own config.
     */
    virtual void setFloodReduction() {}

    // NEIGHBOR

    /**
     * @brief Tears down and restarts all adjacencies on this interface.
     *
     * Forwards to `NeighborTable::resetNeighbors`: every neighbor is dropped
     * back to Down and rediscovered through the normal Hello exchange.  Used
     * during area and process resets, where stale adjacency state must not
     * survive the LSDB flush.
     */
    virtual void resetNeighbors();

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
     * @brief Handles a newly-installed Grace-LSA from a neighbor.
     *
     * Per RFC 3623 SS3, Grace-LSA arrival (not just the LLS restart bit) is the
     * authoritative trigger for entering helper mode. No-op if this
     * interface's `GRACEFUL_RESTART_HELPER` config is disabled, or if no
     * neighbor with the advertising router ID exists.
     *
     * @param advertisingRouter Router ID that originated the Grace-LSA.
     * @param tlv Decoded Grace-LSA TLV contents.
     */
    virtual void handleGraceLsaReceived(uint32_t advertisingRouter, const GraceLsaTlv& tlv);

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

    std::atomic<bool> opaqueEnabled = true;  ///< Whether opaque LSA capability is active on this interface.

    std::atomic<bool> gracefulRestartInProgress{false}; ///< True while this interface's own graceful restart is being signaled (RFC 3623).
    std::chrono::steady_clock::time_point graceDeadline{}; ///< Wall-clock time this interface's own grace period ends.
    GracefulRestartManager graceManager; ///< Grace-LSA originator for this interface (RFC 3623). Must be constructed after `area`/`interfaceId`/`process`.

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
    void forEachNeighbor(Fn&& fn); // NOTE:

    /**
     * @brief Const overload of @ref forEachNeighbor for read-only traversal.
     */
    template <typename Fn>
    void forEachNeighbor(Fn&& fn) const; // NOTE:

    InterfaceFlagManager flags; ///< NOTE: Event flags (e.g. DR changed, neighbor state changed).
    InterfaceFlagManager lsaFlags; ///< NOTE: LSA dirty flags driving re-origination decisions.

    const config::OspfGlobalInterfaceBaseRegistry& globalConfigsBase; ///< Base (version-agnostic) interface config.
    const config::OspfInterfaceBaseRegistry& configsBase;             ///< Version-specific interface config.

    // HELPERS

    const LsdbTable& getLsdb() const;
    const config::OspfRegistry& getProcessConfigs() const;
    const config::OspfAreaRegistry& getAreaConfigs() const;
    core::ProcessQueue& getScheduler() const;
    std::chrono::seconds getHelloInterval() const { return priv.helloTime; }
    std::chrono::seconds getDeadInterval() const { return priv.deadTime; }
    std::optional<__uint128_t> getAuthKey() const { return priv.authKey; }
    std::optional<uint8_t> getAuthKeyId() const { return priv.authKeyId; }

private:

    struct Private
    {
    private:
        friend class OspfInterfaceBase;
        friend class ::Internal_OspfTest;

        std::chrono::seconds helloTime;        ///< NOTE: Configured Hello interval.
        std::chrono::seconds deadTime;         ///< NOTE: Configured Dead interval (must be > helloTime).

        // AUTH
        std::optional<__uint128_t> authKey = std::nullopt;  ///< NOTE: Active authentication key bytes; nullopt if no auth.
        std::optional<uint8_t> authKeyId = std::nullopt;    ///< NOTE: Key ID associated with authKey.
    } priv;
};
} // namespace routing::ospf

#endif // OSPF_INTERFACE_H
