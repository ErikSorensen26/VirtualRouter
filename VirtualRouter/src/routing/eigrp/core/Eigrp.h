/**
 * @file Eigrp.h
 * @brief Core EIGRP process object: owns all per-process subsystems.
 */

/**
 * @defgroup EIGRP EIGRP
 * @ingroup ROUTING
 * @brief EIGRP implementation: DUAL algorithm, RTP reliable transport, topology table.
 */

/**
 * @defgroup EIGRP_CORE EIGRP Core
 * @ingroup EIGRP
 * @brief Core EIGRP process, configuration, interface manager, route manager, and topology.
 */

#ifndef EIGRP_CORE_H
#define EIGRP_CORE_H

#include <utility>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>

#include <ControlScheduler.h>

#include <ByteUtils.hpp>
#include "GlobalAggregator.h"
#include "InterfaceManager.h"
#include "interface/InterfaceManager.h"
#include "Topology.h"
#include "RouteManager.h"
#include "configs/registry/router/EigrpRegistry.h"

namespace core { class VirtualRouter; }
namespace interface { enum class InterfaceType : uint8_t; }
namespace config { class EigrpRegistry; }
class Internal_EigrpTest;

/**
 * @namespace routing::eigrp
 * @brief EIGRP routing protocol implementation.
 *
 * Contains the core process object (@ref Eigrp), topology and DUAL engine,
 * per-interface subsystems, RTP reliable transport, and the neighbor state
 * machine. Both Classic and Named EIGRP modes share the same Eigrp
 * implementation; the difference is only in how CLI configuration is applied.
 */
namespace routing::eigrp
{

/**
 * @brief Container pairing the IPv4 and IPv6 EIGRP process objects for a
 *        single classic-mode Autonomous System number.
 *
 * Either pointer may be null if the corresponding address family has not been
 * configured. The Named flags indicate whether the respective instance was
 * promoted from classic to named configuration.
 */
struct EigrpAutonomousSystem
{
    Eigrp* ipv4 = nullptr;   ///< IPv4 EIGRP process for this AS, or nullptr.
    Eigrp* ipv6 = nullptr;   ///< IPv6 EIGRP process for this AS, or nullptr.
    bool ipv4Named = false;   ///< True if the IPv4 instance uses named-mode config.
    bool ipv6Named = false;   ///< True if the IPv6 instance uses named-mode config.
};

/**
 * @brief Container pairing the IPv4 and IPv6 EIGRP process objects for a
 * @ingroup EIGRP_CORE
 *        single named-mode process group.
 *
 * Either pointer may be null if the address family has not been activated
 * within the named process.
 */
struct EigrpNamed
{
    std::unordered_map<std::string, std::pair<
        Eigrp*, ///< IPv4 EIGRP process for this named group, or nullptr.
        Eigrp*  ///< IPv6 EIGRP process for this named group, or nullptr.
    >> systems;
};

/**
 * @brief Pair of per-address-family @ref EigrpInterface pointers for a single
 * @ingroup EIGRP_CORE
 *        physical interface slot.
 *
 * Used by @ref InterfaceManager to track which EIGRP interface objects are
 * active for each underlying network interface.
 */
struct EigrpInterfaceInstance
{
    EigrpInterface* IPv4; ///< IPv4 EIGRP interface bound to this slot, or nullptr.
    EigrpInterface* IPv6; ///< IPv6 EIGRP interface bound to this slot, or nullptr.
};

/**
 * @brief Central EIGRP process object for one AS number / address family pair.
 * @ingroup EIGRP_CORE
 *
 * Each `Eigrp` instance represents a single running EIGRP process — one AS
 * number combined with one address family (IPv4 or IPv6). Both Classic and
 * Named EIGRP modes share this class; the only difference between them is how
 * CLI configuration reaches the object (via @ref ClassicEigrp or
 * @ref NamedEigrp subclasses).
 *
 * An `Eigrp` instance owns:
 * - @ref EigrpTopology — wraps the @ref DuelEngine and @ref TopologyTable
 * - @ref InterfaceManager — tracks which interfaces participate in this process
 * - A `config::EigrpRegistry&` — process-level configuration (K-values, networks, stub, etc.),
 *   read directly via @ref getConfigs rather than through a separate facade object
 * - @ref GlobalAggregator — manages process-level summary routes
 * - @ref RouteManager — translates topology entries into RIB entries
 * - A `ProcessQueue` scheduler that serializes all protocol work
 *
 * ## Architectural Role
 * `Eigrp` is the top-level owner for all per-process EIGRP state. The VRF
 * creates and destroys it through @ref EigrpAutonomousSystem or
 * @ref EigrpNamed containers. It does not perform packet I/O directly;
 * that is delegated to each @ref EigrpInterface's @ref ReliableTransport.
 *
 * ## Lifecycle & Ownership
 * Call `start()` after construction to subscribe to interface events and
 * begin sending hellos. Call `shutdown()` before destruction; it cancels all
 * timers, tears down neighbors, and unregisters event subscriptions. The
 * destructor assumes `shutdown()` has already been called.
 *
 * ## Concurrency Model
 * All topology and routing decisions are serialized through the internal
 * `ProcessQueue` scheduler. Packet receive callbacks from the hardware layer
 * post work items into that queue before touching any EIGRP state.
 *
 * @see EigrpTopology
 * @see InterfaceManager
 * @see RouteManager
 */
class Eigrp
{
public:
    using InterfaceKey = std::pair<interface::InterfaceType, float>;

    const uint32_t asNumber;                  ///< Autonomous System number. Immutable after construction.
    const types::AddressFamily addressFamily; ///< Address family (IPv4 or IPv6). Immutable after construction.
    const bool namedMode = false;             ///< True when running in named mode. Immutable after construction.

    /**
     * @brief Constructs an EIGRP process for the given AS number and address family.
     * @ingroup EIGRP_CORE
     *
     * Initializes all subsystems (topology, interface manager, config, aggregator,
     * route manager) but does not subscribe to interface events or start timers.
     * Call @ref start() to activate the process.
     *
     * @param as    Autonomous System number.
     * @param af    Address family this process handles (IPv4 or IPv6).
     * @param vrf   Owning VRF; must outlive this object.
     * @param named True if this process was created in named mode.
     */
    Eigrp(uint32_t as, types::AddressFamily af, core::VirtualRouter* vrf, bool named = false);

    /**
     * @brief Destructs the EIGRP process.
     *
     * Assumes @ref shutdown() has been called first. Destroying a running
     * process without calling shutdown first is undefined behavior.
     */
    ~Eigrp();

    /**
     * @brief Schedules an interface-list refresh on the process queue.
     *
     * Config-change entry point: re-evaluates which VRF interfaces should be
     * running EIGRP. Safe to call from any thread.
     */
    void enqueueRefreshInterfaceList();

    /**
     * @brief Schedules a shutdown/restart on the process queue, based on the
     *        current value of the `shutdown` config field.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueShutdown();

    /**
     * @brief Schedules a full topology recalculation on the process queue.
     *
     * Config-change entry point: called when K-values or variance change.
     * Safe to call from any thread.
     */
    void enqueueSyncTopology();

    /**
     * @brief Schedules a passive-interface resync across all interfaces on
     *        the process queue.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueSyncPassive();

    /**
     * @brief Schedules a Router ID resync on the process queue.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueSyncRouterId();

private:
    friend class ::Internal_EigrpTest;

    // Direct children: subsystems owned outright by this process (as members
    // of Eigrp or of its Private block).
    friend class EigrpTopology;
    friend class InterfaceManager;
    friend class GlobalAggregator;
    friend class RouteManager;

    // Named exceptions: grandchildren tightly coupled to process state
    // (packet-layer interface object, and the DUAL engine cluster: DuelEngine
    // itself, its SIA timer manager, and the topology table it owns),
    // matching how OspfProcess friends OspfInterfaceBase directly instead of
    // routing every call through InterfaceManager.
    friend class EigrpInterface;
    friend class DuelEngine;
    friend class TimerManager;
    friend class TopologyTable;

    /**
     * @brief Read-only access to process-level EIGRP settings. Callers read
     *        fields directly (`getConfigs().get<config::Eigrp::FIELD>().load()`)
     *        rather than going through a configuration facade object.
     */
    const config::EigrpRegistry& getConfigs() const { return configs; }

    /**
     * @brief Returns the underlying scheduler queue, for subsystems that mint
     *        their own `ProcessQueue` (e.g. per-interface timers).
     */
    core::ProcessQueue& getScheduler() { return scheduler; }

    /**
     * @brief Blocks until the underlying scheduler queue has drained all
     *        pending and in-flight tasks.
     *
     * Intended for tests; see `core::ProcessQueue::waitIdle()`.
     */
    void waitIdle() const noexcept { scheduler.waitIdle(); }

    /**
     * @brief Router ID value and its configuration origin.
     */
    struct RouterID
    {
        uint32_t id = 0;          ///< 32-bit Router ID (stored as host-byte-order IPv4).
        bool isStatic = false;    ///< True if set by the operator; false if auto-selected.
    };

    /**
     * @brief Returns the current Router ID in host byte order.
     */
    uint32_t routerID() const { return priv.rid.id; }

    inline uint16_t getVirtualRouterID() const { return priv.virtualRouterID; }

    /**
     * @brief Sets a static Router ID, suppressing automatic selection.
     * @param id Host-byte-order 32-bit Router ID value.
     */
    void setRouterID(uint32_t id) { priv.rid.id = id; priv.rid.isStatic = true; }

    /**
     * @brief Clears the static Router ID and triggers automatic recalculation.
     */
    void clearRouterID() { priv.rid.isStatic = false; calculateRID(); }

    /**
     * @brief Selects a Router ID for this process from active interface addresses.
     *
     * Prefers the highest loopback IPv4 address, falling back to the highest
     * non-loopback IPv4 address. Has no effect if the RID was set statically
     * via @ref routerID(uint32_t).
     *
     * @return True if a Router ID was found; false if no suitable address exists.
     */
    bool calculateRID();

    /**
     * @brief Registers a neighbor in the process-wide neighbor map.
     *
     * Called by @ref EigrpInterface when a neighbor transitions to UP so that
     * the process can reach any neighbor regardless of which interface it
     * arrived on.
     *
     * @param neighborIp IP address of the new neighbor.
     * @param neighbor   Pointer to the @ref Neighbor object; must outlive the registration.
     */
    void addGlobalNeighbor(const types::IPAddress& neighborIp, Neighbor* neighbor);

    /**
     * @brief Removes a neighbor from the process-wide neighbor map.
     *
     * @param neighborIp IP address of the neighbor to remove.
     */
    void delGlobalNeighbor(const types::IPAddress& neighborIp);

    /**
     * @brief Broadcasts a set of changed routes to all active EIGRP interfaces.
     *
     * Each interface's @ref ReliableTransport will send an Update packet
     * containing the changed entries, filtered through split-horizon and
     * suppression rules.
     *
     * @param changedRoutes Routes whose topology state has changed and must
     *                      be advertised.
     */
    void broadcastRouteChanges(const std::vector<const RouteInfo*>& changedRoutes);

    /**
     * @brief Re-evaluates the set of physical interfaces and creates or
     *        removes @ref EigrpInterface objects to match.
     *
     * Called on interface up/down events and during process start.
     */
    void refreshInterfaceList();

    /**
     * @brief Performs a full shutdown followed by a fresh start.
     *
     * Used to apply configuration changes that require process restart,
     * such as K-value changes or AS number changes.
     */
    void restart();

    /**
     * @brief Performs periodic housekeeping: prunes stale topology entries
     *        and recomputes any routes whose validity window has expired.
     *
     * Called on a slow-path maintenance timer; not on the forwarding path.
     */
    void runMaintenance();

    /**
     * @brief Checks whether a given IPv4 address falls within any configured
     *        `network` statement range for this process.
     *
     * @param testIp Address to test.
     * @return True if `testIp` is covered by at least one network range.
     */
    bool isInNetworkRange(types::IPv4Address testIp) const;

    /**
     * @brief Returns true if the given interface key is configured as passive.
     */
    bool isPassive(interface::InterfaceKey key) const;

    /**
     * @brief Returns the set of unicast peer addresses configured on the
     *        given interface key.
     */
    std::unordered_set<types::IPAddress> getUnicastNeighbors(interface::InterfaceKey key) const;

    /**
     * @brief Adds an IPv4 network range to this process.
     *
     * Any interface whose primary address falls within `newNetwork` will be
     * activated for EIGRP. Triggers an interface list refresh.
     *
     * @param newNetwork The network prefix to add.
     */
    void addNetworkRange(const types::IPv4Prefix& newNetwork);

    /**
     * @brief Removes a previously configured IPv4 network range.
     *
     * Interfaces that no longer match any network range are deactivated.
     *
     * @param delNetwork The network prefix to remove.
     */
    void delNetworkRange(const types::IPv4Prefix& delNetwork);

    /**
     * @brief Removes all configured network ranges and deactivates all interfaces.
     */
    void clearNetworks();

    /**
     * @brief Configures or disables stub router mode for this process.
     *
     * @param isStub              True to enable stub mode.
     * @param advertiseConnected  Advertise connected prefixes (default true).
     * @param advertiseStatic     Advertise static redistributed routes (default true).
     * @param advertiseSummary    Advertise summary routes (default true).
     * @param advertiseRedistributed Advertise all redistributed routes (default true).
     */
    void enableStub(bool isStub, bool advertiseConnected = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);

    /**
     * @brief Marks or unmarks an interface as passive.
     *
     * @param key Interface key.
     * @param add True to make passive, false to remove the passive flag.
     */
    void setPassiveInterface(interface::InterfaceKey key, bool add = true);

    /**
     * @brief Enables a unicast static neighbor relationship on an interface.
     *
     * @param neighborIp IP address of the peer.
     * @param key        Interface key the peer is reachable through.
     */
    void enableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key);

    /**
     * @brief Removes a unicast static neighbor relationship.
     *
     * @param neighborIp IP address of the peer to remove.
     * @param key        Interface key the peer was configured on.
     */
    void disableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key);

    GlobalAggregator& getAggregator() { return priv.aggregator; }
    EigrpTopology& getTopology() { return priv.topology; }

    core::ProcessQueue scheduler; ///< Serializes all EIGRP protocol work for this process.

    config::EigrpRegistry& configs; ///< Live reference to this process's configuration registry.

protected:
    // start()/shutdown() are genuine inheritance access: ClassicEigrp/
    // NamedEigrp are the only subclasses of Eigrp and reach these through
    // `this->`. routingInstance and getIfaceMgr() also need to be reachable
    // by NamedEigrp::configureInterface() the same way; they happen to be
    // used by friended collaborators too, which protected still permits.

    /**
     * @brief Activates the process: subscribes to interface events and
     *        populates the initial interface list.
     */
    void start();

    /**
     * @brief Gracefully shuts down the process.
     *
     * Posts the actual teardown onto this process's @ref ProcessQueue and
     * blocks until it completes. Safe to call from any thread that is not
     * itself running on this process's queue.
     */
    void shutdown();

    core::VirtualRouter* const routingInstance; ///< Owning VRF instance; immutable after construction.

    InterfaceManager& getIfaceMgr() { return priv.ifaceMgr; }

private:
    struct Private
    {
    private:
        friend class Eigrp;

        Private(Eigrp& eigrp);

        EigrpTopology topology;     ///< Topology table + DUAL engine.
        InterfaceManager ifaceMgr;  ///< Manages per-interface EIGRP state.
        GlobalAggregator aggregator; ///< Process-level route summarization.

        RouterID rid;                       ///< Current Router ID and its origin.
        uint16_t virtualRouterID = 0x0000;  ///< Virtual Router ID carried in EIGRP packets.

        /**
         * @brief Performs the actual shutdown teardown (deactivates all
         *        interfaces).
         *
         * Must only be called from within a task already running on
         * @ref scheduler's queue (i.e. posted via @ref selfRef), or after
         * @ref selfRef has been released and no other queue work can be
         * in-flight (e.g. from `~Eigrp()`). Calling this directly from an
         * arbitrary thread races with `refreshInterfaceList()`.
         */
        void shutdown();

        uint32_t ifUpId, ifDownId, ipReadyId, ipDelId; ///< Interface event subscription IDs.
    } priv;

    RouteManager routeManager; ///< Translates topology successors into RIB entries.
    std::unordered_map<types::IPAddress, Neighbor*> allNeighbors; ///< All UP neighbors across all interfaces.
};

/**
 * @brief Classic-mode EIGRP process variant.
 * @ingroup EIGRP_CORE
 *
 * Thin subclass of @ref Eigrp that adds the `initializeEigrp()` entry point
 * used by the CLI when creating a classic-mode `router eigrp <asn>` block.
 * Classic mode and named mode share all runtime behavior; only configuration
 * wiring differs.
 */
class ClassicEigrp : public Eigrp
{
public:
    /**
     * @brief Constructs a classic-mode EIGRP process.
     *
     * @param as  AS number (taken by reference for CLI convenience).
     * @param af  Address family.
     * @param vrf Owning VRF.
     */
    ClassicEigrp(uint32_t& as, types::AddressFamily af, core::VirtualRouter* vrf) : Eigrp(as, af, vrf) {}

    /**
     * @brief Applies initial classic-mode configuration and calls @ref start().
     */
    void initializeEigrp();

    /**
     * @brief Overrides shutdown to perform classic-mode-specific teardown
     *        before delegating to @ref Eigrp::shutdown().
     */
    void shutdown();
};

/**
 * @brief Named-mode EIGRP process variant.
 * @ingroup EIGRP_CORE
 *
 * Extends @ref Eigrp with the named-mode `router eigrp <name>` CLI entry point
 * and per-interface configuration support. Named mode allows a single process
 * name to host multiple address families and provides per-AF address-family
 * configuration blocks.
 */
class NamedEigrp : public Eigrp
{
private:
    std::string processName; ///< CLI-assigned name for this named EIGRP process.

public:
    /**
     * @brief Constructs a named-mode EIGRP process.
     *
     * @param as        AS number (taken by reference for CLI convenience).
     * @param af        Address family.
     * @param name      Named-mode process name (e.g., "CAMPUS").
     * @param vrf       Owning VRF.
     * @param multicast True to use multicast hellos; false for unicast-only.
     */
    NamedEigrp(uint32_t& as, types::AddressFamily af, const std::string& name, core::VirtualRouter* vrf, bool multicast);

    /**
     * @brief Applies initial named-mode configuration and calls @ref start().
     */
    void initializeEigrp();

    /**
     * @brief Overrides shutdown to perform named-mode-specific teardown
     *        before delegating to @ref Eigrp::shutdown().
     */
    void shutdown();

    /**
     * @brief Applies per-interface named-mode configuration to the specified
     *        interface.
     *
     * @param interfaceId System interface key of the target interface.
     */
    void configureInterface(uint32_t interfaceId);
};
} // namespace routing::eigrp

#endif // EIGRP_CORE_H

