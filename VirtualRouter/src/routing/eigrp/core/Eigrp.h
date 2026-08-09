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

#include <ControlScheduler.h>

#include <ByteUtils.hpp>
#include "GlobalAggregator.h"
#include "EigrpConfig.h"
#include "InterfaceManager.h"
#include "interface/InterfaceManager.h"
#include "Topology.h"
#include "RouteManager.h"

namespace core { class VirtualRouter; }
namespace interface { enum class InterfaceType : uint8_t; }
namespace config { void EigrpShutdown(void* e); }
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
 * - @ref EigrpTopology — wraps the @ref DualEngine and @ref TopologyTable
 * - @ref InterfaceManager — tracks which interfaces participate in this process
 * - @ref EigrpConfig — process-level configuration (K-values, networks, stub, etc.)
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
    friend class ::Internal_EigrpTest;
    friend void ::config::EigrpShutdown(void* e);

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
    virtual ~Eigrp();

    /**
     * @brief Activates the process: subscribes to interface events and
     *        populates the initial interface list.
     */
    virtual void start();

    /**
     * @brief Gracefully shuts down the process.
     *
     * Sends poison-reverse updates to all neighbors, cancels all timers,
     * brings down all neighbors, and unsubscribes from interface events.
     * Must be called before destruction.
     *
     * Posts the actual teardown onto this process's @ref ProcessQueue and
     * blocks until it completes, so it is serialized against any in-flight
     * or pending work posted via @ref getScheduler() (e.g.
     * `refreshInterfaceList()` triggered by interface events). Safe to call
     * from any thread that is not itself running on this process's queue.
     */
    virtual void shutdown();

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
     * @brief Checks whether a given IPv4 address falls within any configured
     *        `network` statement range for this process.
     *
     * @param testIp Address to test.
     * @return True if `testIp` is covered by at least one network range.
     */
    bool isInNetworkRange(types::IPv4Address testIp);

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

    EigrpConfig& getGlobalConfigMgr() { return configMgr; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    GlobalAggregator& getAggregator() { return aggregator; }
    EigrpTopology& getTopology() { return topology; }

public:
    /**
     * @brief Router ID value and its configuration origin.
     */
    struct RouterID
    {
        uint32_t id = 0;          ///< 32-bit Router ID (stored as host-byte-order IPv4).
        bool isStatic = false;    ///< True if set by the operator; false if auto-selected.
    };

    inline uint16_t getVirtualRouterID() const { return virtualRouterID; }

    /**
     * @brief Writes the Router ID as 4 big-endian bytes into `out`.
     * @param[out] out Destination buffer; caller must supply at least 4 bytes.
     * @return `out` for chaining.
     */
    inline uint8_t* routerID(uint8_t* out) const { utils::writeU32(out, rid.id); return out; }
    inline uint32_t routerID() const { return rid.id; }

    /**
     * @brief Sets a static Router ID, suppressing automatic selection.
     * @param id Host-byte-order 32-bit Router ID value.
     */
    inline void routerID(uint32_t id) { rid.id = id; rid.isStatic = true; }

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

    bool isNamed() const { return namedMode; }
    uint32_t getAS() const { return asNumber; }
    types::AddressFamily getAF() const { return addressFamily; }

    /**
     * @brief Clears the static Router ID and triggers automatic recalculation.
     */
    void clearRouterID() { rid.isStatic = false; calculateRID(); }

    core::VirtualRouter* routingInstance; ///< Owning VRF instance.

private:
    const uint32_t asNumber;             ///< Autonomous System number.
    const types::AddressFamily addressFamily; ///< Address family (IPv4 or IPv6).

    core::ProcessQueue scheduler; ///< Serializes all EIGRP protocol work for this process.

    EigrpTopology topology;     ///< Topology table + DUAL engine.
    InterfaceManager ifaceMgr;  ///< Manages per-interface EIGRP state.
    EigrpConfig configMgr;      ///< Process-level configuration facade.
    GlobalAggregator aggregator; ///< Process-level route summarization.

    bool namedMode = false;              ///< True when running in named mode.
    RouterID rid;                        ///< Current Router ID and its origin.
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
    void shutdownInternal();

    uint32_t ifUpId, ifDownId, ipReadyId, ipDelId; ///< Interface event subscription IDs.
public:
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

