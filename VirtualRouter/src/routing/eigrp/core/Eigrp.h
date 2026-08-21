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
#include <memory>
#include <unordered_set>
#include <vector>

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
namespace config { struct EigrpRegistry; }
class Internal_EigrpTest;

/**
 * @namespace routing::eigrp
 * @brief EIGRP routing protocol implementation.
 *
 * Contains the core process object (@ref Eigrp), topology and DUAL engine,
 * per-interface subsystems, RTP reliable transport, and the neighbor state
 * machine. Classic and wide metric encoding are both handled by the same
 * Eigrp implementation; the format used with a given peer is negotiated
 * per-neighbor from the version TLV carried in that peer's HELLO packets
 * (see @ref Neighbor::Version), not by any static process-level mode.
 */
namespace routing::eigrp
{

/**
 * @brief Container pairing the IPv4 and IPv6 EIGRP process objects for a
 *        single Autonomous System number.
 * @ingroup EIGRP_CORE
 *
 * Either pointer may be null if the corresponding address family has not been
 * configured.
 */
struct EigrpAutonomousSystem
{
    Eigrp* ipv4 = nullptr;   ///< IPv4 EIGRP process for this AS, or nullptr.
    Eigrp* ipv6 = nullptr;   ///< IPv6 EIGRP process for this AS, or nullptr.
};

/**
 * @brief Pair of per-address-family @ref EigrpInterface pointers for a single
 *        physical interface slot.
 * @ingroup EIGRP_CORE
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
 * @brief Lookup context for a statically configured unicast EIGRP neighbor.
 * @ingroup EIGRP_CORE
 *
 * Stored in @ref Eigrp::nbrContext, keyed by neighbor address, so that a
 * unicast neighbor can be resolved back to its owning process and interface
 * without a table scan.
 */
struct EigrpNeighborContext
{
    EigrpNeighborContext(routing::eigrp::Eigrp& e, const types::IPAddress& a)
        : eigrp(e), addr(a)
    {}
    routing::eigrp::Eigrp& eigrp;         ///< Owning EIGRP process.
    types::IPAddress addr;                ///< Neighbor's configured IP address.
    interface::InterfaceKey key;          ///< Interface the neighbor is configured under.
};

/**
 * @brief Central EIGRP process object for one AS number / address family pair.
 * @ingroup EIGRP_CORE
 *
 * Each `Eigrp` instance represents a single running EIGRP process — one AS
 * number combined with one address family (IPv4 or IPv6). Classic and named
 * CLI configuration both reach this same class; the two syntaxes just write
 * into the same underlying @ref EigrpConfig.
 *
 * An `Eigrp` instance owns:
 * - @ref EigrpTopology — wraps the @ref DualEngine and @ref TopologyTable
 * - @ref InterfaceManager — tracks which interfaces participate in this process
 * - A `config::EigrpRegistry&` — process-level configuration (K-values, networks, stub, etc.),
 *   read directly via @ref getConfigs rather than through a separate facade object
 * - @ref GlobalAggregator — manages process-level summary routes
 * - @ref RouteManager — translates topology entries into RIB entries
 * - A `ProcessQueue` scheduler that serializes all protocol work
 *
 * ## Architectural Role
 * `Eigrp` is the top-level owner for all per-process EIGRP state. The VRF
 * creates and destroys it through @ref EigrpAutonomousSystem. It does not
 * perform packet I/O directly; that is delegated to each @ref EigrpInterface's
 * @ref ReliableTransport.
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

    const uint32_t asNumber;                  ///< Autonomous System number. Immutable after construction.
    const types::AddressFamily addressFamily; ///< Address family (IPv4 or IPv6). Immutable after construction.
    const bool namedMode = false;             ///< True when running in named mode. Immutable after construction.

    /**
     * @brief Constructs an EIGRP process for the given AS number and address family.
     *
     * Initializes all subsystems (topology, interface manager, config, aggregator,
     * route manager) but does not subscribe to interface events or start timers.
     * Call @ref start() to activate the process.
     *
     * @param reg   Process-level configuration registry; must outlive this object.
     * @param as    Autonomous System number.
     * @param af    Address family this process handles (IPv4 or IPv6).
     * @param vrf   Owning VRF; must outlive this object.
     */
    Eigrp(config::EigrpRegistry& reg, uint16_t as, types::AddressFamily af, core::VirtualRouter* vrf);

    /**
     * @brief Destructs the EIGRP process.
     *
     * Assumes @ref shutdown() has been called first. Destroying a running
     * process without calling shutdown first is undefined behavior.
     */
    ~Eigrp();

    /**
     * @brief Schedules an auto-summarization enable/disable on the process queue.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueSetAutoSummarization(bool enable);

    /**
     * @brief Schedules a shutdown/restart on the process queue, based on the
     *        current value of the `shutdown` config field.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueSetShutdown(bool shut);

    /**
     * @brief Schedules a full topology recalculation on the process queue.
     *
     * Config-change entry point: called when K-values or variance change.
     * Safe to call from any thread.
     */
    void enqueueSyncTopology();

    /**
     * @brief Schedules adding or removing a statically configured unicast
     *        neighbor on @p key's interface, on the process queue.
     *
     * Config-change entry point. Safe to call from any thread.
     *
     * @param addr Neighbor's IP address.
     * @param key  Interface the neighbor is configured under.
     * @param add  True to create the neighbor, false to delete it.
     */
    void enqueueSetUnicastNeighbor(const types::IPAddress& addr, interface::InterfaceKey* key);

    /**
     * @brief Schedules a passive-interface toggle for @p iface on the process
     *        queue; a no-op if PASSIVE_INTERFACE is overridden at the interface level.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueSetPassive(const interface::InterfaceKey& iface, bool passive);

    /**
     * @brief Schedules a Router ID resync on the process queue.
     *
     * Config-change entry point. Safe to call from any thread.
     */
    void enqueueSetRouterId(std::optional<uint32_t> rid);

    /**
     * @brief Schedules creating or destroying @p iface's EIGRP interface on
     *        the process queue, based on whether @p reg is non-null.
     *
     * Config-change entry point. Safe to call from any thread.
     *
     * @param iface Interface being activated/deactivated for this AF.
     * @param reg   Registry to bind the new interface to, or nullptr to
     *              destroy the existing one.
     */
    void enqueueSetAfInterface(const interface::InterfaceKey& iface, config::EigrpInterfaceRegistry* reg);

    /**
     * @brief Returns the unicast neighbor context for @p addr, creating an
     *        empty one bound to this process if none exists yet.
     */
    EigrpNeighborContext& getNeighborContext(const types::IPAddress& addr);

    /**
     * @brief Erases the unicast neighbor context for @p addr, if present.
     */
    void eraseNeighborContext(const types::IPAddress& addr);

    /**
     * @brief Entry point for an EIGRP packet received on one of this process's interfaces.
     *
     * Called from the hardware RX path, which sits outside the EIGRP subsystem
     * tree and must not reach into `EigrpInterface`/`ReliableTransport` directly
     * or touch neighbor/topology state itself. This posts the actual handling
     * onto `scheduler` so that state is only ever touched from this process's
     * own control thread, per the concurrency model documented on this class.
     *
     * @param ifaceKey    Interface the packet arrived on.
     * @param packetCopy  Owned copy of the packet region from the start of the
     *                    IP payload through the end of the EIGRP TLV trailer.
     *                    The original RX buffer is transient and reused as
     *                    soon as the caller returns, so it cannot be deferred
     *                    onto the scheduler by reference - the caller copies
     *                    it up front instead.
     * @param eigrpOffset Byte offset of the EIGRP header within `*packetCopy`.
     * @param trailSize   Size of the TLV trailer following the EIGRP header.
     * @param neighborIp  Source IP address the packet arrived from.
     * @param multicast   True if the packet was sent to the EIGRP multicast group.
     */
    void handleIncomingPacket(interface::InterfaceKey ifaceKey,
                               std::shared_ptr<std::vector<uint8_t>> packetCopy,
                               size_t eigrpOffset, size_t trailSize,
                               types::IPAddress neighborIp, bool multicast);

private:
    friend class ::Internal_EigrpTest;

    // Direct children
    friend class EigrpTopology;
    friend class InterfaceManager;
    friend class GlobalAggregator;
    friend class RouteManager;

    // Named exceptions
    friend class EigrpInterface;
    friend class DualEngine;
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
     * @brief Returns true if the given interface key is configured as passive.
     */
    bool isPassive(interface::InterfaceKey key) const;

    /**
     * @brief Returns the set of unicast peer addresses configured on the
     *        given interface key.
     */
    std::unordered_set<types::IPAddress> getUnicastNeighbors(interface::InterfaceKey key) const;

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

    GlobalAggregator& getAggregator() { return priv.aggregator; }
    EigrpTopology& getTopology() { return priv.topology; }

    core::ProcessQueue scheduler; ///< Serializes all EIGRP protocol work for this process.

    config::EigrpRegistry& configs; ///< Live reference to this process's configuration registry.

protected:

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
         * @ref Eigrp::scheduler's queue, or after that queue has been
         * released and no other queue work can be in-flight (e.g. from
         * `~Eigrp()`). Calling this directly from an arbitrary thread races
         * with the interface up/down event handlers.
         */
        void shutdown();

        uint32_t ifUpId, ifDownId, ipReadyId, ipDelId; ///< Interface event subscription IDs.
    } priv;

    RouteManager routeManager; ///< Translates topology successors into RIB entries.
    
    std::unordered_map<types::IPAddress, EigrpNeighborContext> nbrContext; ///< Unicast neighbor contexts, keyed by neighbor address.
    std::unordered_map<types::IPAddress, Neighbor*> allNeighbors; ///< All UP neighbors across all interfaces.
};
} // namespace routing::eigrp

#endif // EIGRP_CORE_H

