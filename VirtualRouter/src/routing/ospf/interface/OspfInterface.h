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

#include "configs/registry/router/OspfRegistry.h"
#include "ospf/FlagManager.hpp"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/interface/InterfaceTimers.h"
#include "ospf/interface/GracefulRestartManager.h"
#include "InterfaceId.hpp"
#include "ospf/area/Area.h"
#include "OspfInterfaceBase.h"

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
class OspfInterface : public OspfInterfaceBase
{
    friend class ::Internal_OspfTest;
public:

    /**
     * @brief Constructs an OSPF interface and attaches it to its area.
     * @ingroup OSPF_INTERFACE
     *
     * Resolves the `Area` reference from the process, selects the correct
     * `PacketDispatcher` subclass (OSPFv2 or OSPFv3), and initialises cost
     * and timer values from the interface's configuration registry.
     *
     * @param proc     The OSPF process that owns this interface.
     * @param iface    The underlying hardware/logical interface.
     * @param id       Composite key (hardware index + area) for this interface.
     */
    OspfInterface(OspfProcess& proc, interface::Interface& iface,
                  const OspfInterfaceId& id);

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
     * @brief Schedules `syncPassive()` on the process queue.
     *
     * Config-change entry point for `passive-interface`.  Drops all
     * neighbors and stops the Hello timer when the interface becomes
     * passive, or restarts Hello when it becomes active, all on the
     * scheduler thread.
     */
    void enqueueSyncPassive();

    /**
     * @brief Schedules an LSA re-origination for this interface on the process queue.
     *
     * Config-change entry point for `prefix-suppression`.  Recomputes this
     * interface's Router LSA contribution so its connected prefix is
     * advertised or withheld according to the new setting.
     */
    void enqueueSyncPrefixSuppression();

    const types::IPPrefix interfaceAddress; ///< Primary IP prefix (address + mask) assigned to this interface.
    interface::Interface& iface; ///< Physical Interface that resides under this.

    // GETTERS

    uint16_t getCost() const override { return priv.cost.load(std::memory_order_relaxed); }
    uint32_t getDrRid() const { return dr.rid.load(std::memory_order_acquire); }
    uint32_t getBdrRid() const { return bdr.rid.load(std::memory_order_acquire); }
    types::IPAddress getDrIp() const { return dr.ip.load(std::memory_order_relaxed); }
    types::IPAddress getBdrIp() const { return bdr.ip.load(std::memory_order_relaxed); }
    bool getIsDr() const { return priv.isDr.load(std::memory_order_acquire); }
    bool getIsBdr() const { return priv.isBdr.load(std::memory_order_acquire); }

    // OspfInterfaceBase overrides

    config::ospf::NetworkType getNetworkType() const override { return configs.get<config::OspfInterface::NETWORK>().load(); }
    bool getPassive() const override { return configs.get<config::OspfInterface::PASSIVE>().load(); }
    uint8_t getPriority() const override { return configs.get<config::OspfInterface::PRIORITY>().load(); }
    bool getMtuIgnore() const override { return configs.get<config::OspfInterface::MTU_IGNORE>().load(); }
    bool getDatabaseFilter() const override { return configs.get<config::OspfInterface::DATABASE_FILTER>().load(); }
    bool getDemandCircuitIgnore() const override { return configs.get<config::OspfInterface::DEMAND_CIRCUIT_IGNORE>().load(); }
    bool getLls() const override { auto lls = globalConfigs.get<config::OspfGlobalInterface::LLS>(); return lls.hasValue() ? lls.load() : getProcessConfigs().get<config::Ospf::LLS>().load(); };
    interface::Interface* getTransmitInterface() const override { return &iface; }
    const types::IPPrefix& getTransmitAddress() const override { return interfaceAddress; }
    bool getIsMulticast() const override { return isMulticast.load(std::memory_order_relaxed); }

private:

    friend class OspfProcess;
    friend class InterfaceManager;
    friend class NeighborTable;

    // INTERFACE

    /**
     * @brief Runs the DR/BDR election algorithm for this broadcast segment.
     *
     * Implements the two-pass election defined in RFC 2328 §9.4. Updates
     * `dr`, `bdr`, `isDr`, and `isBdr` and triggers any required LSA
     * re-origination when the election result changes.
     */
    void election() override;

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
     * @brief Reconciles the interface network type (broadcast, P2P, NBMA,
     *        P2MP) with the current configuration.
     *
     * May trigger a DR/BDR election or skip it depending on the new type.
     */
    void syncNetworkType();

    /**
     * @brief Loads the passive state for the interface.
     *
     * Reads the passive state of the interface and updates accordingly.
     * May need to reset the neighbor if passive is enabled/disabled.
     */
    void syncPassive();

    /**
     * @brief Configures flood-reduction mode for the specified interface.
     *
     * Sets the DoNotAge bit on all self-originated LSAs flooded out of `iface`
     * when flood reduction is enabled (RFC 2328 Appendix B).
     */
    void setFloodReduction() override;

    /**
     * @brief Gates helper-mode entry on `GRACEFUL_RESTART_HELPER` before
     *        delegating to the shared `OspfInterfaceBase` logic.
     */
    void handleGraceLsaReceived(uint32_t advertisingRouter, const GraceLsaTlv& tlv) override;

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

    const config::OspfGlobalInterfaceRegistry& globalConfigs; ///< Base (version-agnostic) interface config.
    const config::OspfInterfaceRegistry& configs;             ///< Version-specific interface config.

private:

    OspfInterface(OspfProcess& proc, interface::Interface& iface,
                  const OspfInterfaceId& id, const config::OspfGlobalInterfaceRegistry&);

    struct Private
    {
    private:
        friend class OspfInterface;
        friend class ::Internal_OspfTest;

        std::atomic<uint16_t> cost;            ///< Current interface cost in OSPF metric units.

        // DR / BDR
        std::atomic<bool> isDr = false;       ///< True when this router is the DR on this segment.
        std::atomic<bool> isBdr = false;      ///< True when this router is the BDR on this segment.
    } priv;
};
} // namespace routing::ospf

#endif // OSPF_INTERFACE_H
