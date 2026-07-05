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
#include "ospf/area/FlagManager.h"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/interface/InterfaceTimers.h"
#include "InterfaceId.hpp"

namespace interface { class Interface; }

namespace routing::ospf
{
class PacketDispatcher;
class OspfProcess;
class Topology;
class Area;
class Neighbor;

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

    const OspfInterfaceId id;      ///< Immutable composite key for this interface.
    const uint32_t interfaceId;    ///< Hardware interface index, mirrored from id.interfaceId.

    OspfProcess& getProcess() { return process; }
    const OspfProcess& getProcess() const { return process; }
    NeighborTable& getNTable() { return ntable; }
    const NeighborTable& getNTable() const { return ntable; }
    InterfaceTimers& getTimers() { return tmgr; }
    PacketDispatcher& getDispatcher() { return *dispatcher; }
    InterfaceFlagManager& getFlags() { return flags; }
    const InterfaceFlagManager& getFlags() const { return flags; }
    InterfaceFlagManager& getLsaFlags() { return lsaFlags; }
    const InterfaceFlagManager& getLsaFlags() const { return lsaFlags; }
    config::OspfInterfaceRegistry& getConfigs() { return configs; }
    const config::OspfInterfaceRegistry& getConfigs() const { return configs; }
    config::OspfInterfaceBaseRegistry& getBaseConfigs() { return baseConfigs; }
    const config::OspfInterfaceBaseRegistry& getBaseConfigs() const noexcept { return baseConfigs; }

    /**
     * @brief Returns the OSPF area this interface participates in.
     *
     * Resolved from the process area map using `id.area`. The returned
     * reference is valid for the lifetime of this interface.
     */
    Area& getArea();
    uint32_t getAreaId() const { return id.area; }
    interface::Interface& getIface() { return iface; }
    const interface::Interface& getIface() const { return iface; }

    const types::IPPrefix interfaceAddress; ///< Primary IP prefix (address + mask) assigned to this interface.

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
     * @brief Enables or disables passive mode on this interface.
     *
     * A passive interface sends Hello packets but does not form adjacencies.
     * When transitioning to passive, all existing neighbors are torn down.
     *
     * @param passive  True to enable passive mode, false to disable.
     */
    void setPassiveMode(bool passive);

    /**
     * @brief Atomic DR/BDR designation: router ID and IP address pair.
     *
     * Both fields are updated together during DR/BDR election. The IP address
     * is stored as a 128-bit value to support both IPv4 and IPv6 uniformly.
     */
    struct Designation { std::atomic<uint32_t> rid; std::atomic<__uint128_t> ip; };
    Designation dr;  ///< Current DR: router ID and interface IP.
    Designation bdr; ///< Current BDR: router ID and interface IP.

    std::atomic<bool> isDr = false;       ///< True when this router is the DR on this segment.
    std::atomic<bool> isBdr = false;      ///< True when this router is the BDR on this segment.
    std::atomic<bool> isVirtual = false;  ///< True for OSPFv3 virtual links.

    // AUTH
    std::optional<__uint128_t> authKey = std::nullopt;  ///< Active authentication key bytes; nullopt if no auth.
    std::optional<uint8_t> authKeyId = std::nullopt;    ///< Key ID associated with authKey.

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

    uint16_t cost;                         ///< Current interface cost in OSPF metric units.
    std::chrono::seconds helloTime;        ///< Configured Hello interval.
    std::chrono::seconds deadTime;         ///< Configured Dead interval (must be > helloTime).
    DcDecision demandCircuit = DcDecision::UNDECIDED; ///< Demand-circuit negotiation outcome.
    bool floodReduction = false;           ///< Whether flood reduction (RFC 2328 §G.2) is active.

private:
    OspfProcess& process;

    PacketDispatcher* dispatcher = nullptr; ///< Version-specific packet dispatcher; allocated at construction.
    Area& area;

    InterfaceFlagManager flags;    ///< Event flags (e.g. DR changed, neighbor state changed).
    InterfaceFlagManager lsaFlags; ///< LSA dirty flags driving re-origination decisions.
    NeighborTable ntable;
    InterfaceTimers tmgr;
    interface::Interface& iface;

    config::OspfInterfaceBaseRegistry& baseConfigs; ///< Base (version-agnostic) interface config.
    config::OspfInterfaceRegistry& configs;         ///< Version-specific interface config.
};

} // namespace routing::ospf

#endif // OSPF_INTERFACE_H
