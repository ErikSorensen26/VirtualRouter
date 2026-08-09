/**
 * @file VirtualRouter.h
 * @brief Per-VRF routing instance owning protocols, RIB, and interface namespace.
 */

/**
 * @defgroup ROUTING Routing Protocols
 * @brief All routing protocol implementations (EIGRP, OSPF, BGP, RIP).
 */

#ifndef VIRTUAL_ROUTER_H
#define VIRTUAL_ROUTER_H

#include <string>
#include <set>
#include <AddressFamily.hpp>

#include "tcp/Tcp.h"
#include "routing/RoutingTable.hpp"
#include "interface/InterfaceManager.h"

namespace config { struct VrfRegistry; struct GlobalRegistry; }

namespace interface { class Interface; enum class InterfaceType : uint8_t; }

namespace routing::eigrp {
    struct EigrpAutonomousSystem;
    struct EigrpNamed;
}
namespace routing::ospf {
    class OspfProcess;
    class OspfV3Instance;
}

namespace core
{

class Global;
class ControlScheduler;

/**
 * @brief Represents a complete routing instance (VRF) within the virtual router.
 * @ingroup CORE
 *
 * A VirtualRouter mirrors the behavior of a VRF (Virtual Routing and Forwarding instance).
 *
 * Each VirtualRouter instance contains:
 * - Independent RIB/FIB routing tables
 * - Independent TCP socket/connection tables
 * - Its own interface namespace (only interfaces explicitly added belong to this VRF)
 * - Independent enablement of IPv4 and IPv6 address families
 * - Independent instances of routing protocols (EIGRP classic, wide metrics, named mode)
 *
 * ## Architectural Role
 * VirtualRouter serves as the boundary between:
 * - The **global system controller** (`Global`) and
 * - **Per-VRF control-plane logic**, such as routing protocols.
 *
 * It isolates routing information so that multiple routing contexts can exist in parallel,
 * enabling realistic testing, protocol isolation, and multi-tenant logical topologies.
 *
 * ## Lifecycle & Ownership
 * - Owned and created by the `Global` object.
 * - Interfaces are not owned by VirtualRouter; they are globally allocated, but *attached*
 *   to a VRF through this class.
 * - EIGRP Autonomous Systems and Named configurations **are owned** by the VirtualRouter
 *   and destroyed in the destructor.
 * - When destroyed, the VRF removes any attached interfaces from the global interface
 *   registry to ensure no stale forwarding references can occur.
 *
 * ## Concurrency Model
 * VirtualRouter is heavily multi-threaded and exposes fine-grained locking:
 *
 * - `interfaceMutex`
 *      Guards lookup, insertion, and deletion of interfaces for this VRF.
 *      Supports concurrent readers.
 *
 * - `eigrpAutonomousSystemMutex`
 *      Protects non-named EIGRP AS structures.
 *
 * - `eigrpNamedMutex`
 *      Protects EIGRP Named-mode configuration blocks.
 *
 * All public methods that access shared state acquire the appropriate locks.
 *
 * ## Routing Protocol Integration
 * The VRF owns all EIGRP instances (classic mode and named mode).  
 * They interact with:
 * - `routingTable` for RIB insertion/removal
 * - Attached interfaces for neighbor discovery and hello processing
 * - Global configuration for timing and authentication policies
 *
 * ## Fast Path vs. Slow Path
 * - **Slow path**: Creation/destruction of interfaces, protocol instances, VRF-wide config.
 * - **Fast path**: Route lookups and protocol adjacency reads (handled inside protocol modules).
 */
class VirtualRouter
{
public:
    friend class EigrpTest; ///< Test harness access for controlled protocol testing.

    /**
     * @brief Constructs a VirtualRouter instance.
     *
     * Initializes an empty VRF with:
     * - No attached interfaces
     * - Empty routing table
     * - No EIGRP instances
     *
     * IPv4 is enabled by default because it is the most common base AF.
     *
     * @param global Reference to the Global controller that owns this VRF.
     * @param name   Unique VRF name (e.g., "default").
     */
    VirtualRouter(Global& global, const std::string& name);

    /**
     * @brief Destructor for the VirtualRouter.
     *
     * Performs a full teardown of the VRF:
     * - Detaches all interfaces and removes them from the global registry
     * - Deletes all EIGRP autonomous systems (classic mode)
     * - Deletes all EIGRP named instances
     *
     * Interfaces are removed via `global.removeInterface()` to ensure hardware
     * resources, protocol references, and NDP/ARP caches also get cleaned up properly.
     *
     * All protocol teardown is performed under the appropriate locks to prevent races
     * with active protocol threads or timer callbacks.
     */
    ~VirtualRouter();

    /**
     * @brief Returns if this VRF is empty and able to be deconstructed.
     *
     * If this VRF is not empty, meaning something is referencing it, then
     * it cannot safely be deleted.
     *
     * @return true if empty, otherwise false.
     */
    bool empty();

    /**
     * @brief Calculates a Router ID for a routing process.
     *
     * Performs a full router ID calculation covering all interfaces in the VRF
     *  - First will look for the highest loopback IPv4 address.
     *  - Second will look for the highest ethernet IPv4 address.
     *  - If no IPv4 addresses exist then it will need to be manually defined for the routing process.
     *
     * @note Duplicate RIDs are possible, there is no claiming ips for RIDs, if the
     * best RID address still exists when a new process calculates, then that could
     * create duplicates. To prevent duplicates, manually define the Router ID for the routing process.
     *
     * Routing IDs DO NOT change as interfaces change.
     */
    bool calculateRID(uint32_t& rid);

    /**
     * @brief Set of enabled address families (IPv4, IPv6).
     *
     * Determines which protocol stacks are available in this VRF.
     * Protocols will not initialize for disabled AFs.
     */
    std::set<types::AddressFamily> enabledAddressFamilies;

    // EIGRP AUTONOMOUS SYSTEMS

    /**
     * @brief Synchronizes all classic and named EIGRP IPv4 instances with the current interface state.
     *
     * Called whenever the interface list or EIGRP configuration changes. Re-evaluates
     * which interfaces should be participating in EIGRP IPv4 based on configured
     * network statements and enabled address families, then updates each running
     * instance accordingly.
     */
    void refreshEigrpV4();

    /**
     * @brief Synchronizes all classic and named EIGRP IPv6 instances with the current interface state.
     *
     * Equivalent to @ref refreshEigrpV4 for the IPv6 data plane. Re-evaluates
     * interface participation for all EIGRP IPv6 processes and updates
     * neighbor relationships and topology entries as needed.
     */
    void refreshEigrpV6();

    /**
     * @brief Pushes current interface metrics and state into all EIGRP IPv6 interface managers.
     *
     * Called after interface configuration changes (MTU, bandwidth, delay) to
     * ensure EIGRP IPv6 recomputes its composite metric and redistributes
     * updated routes if anything changed.
     */
    void refreshEigrpV6Interfaces();

    // EIGRP CLASSIC SYSTEMS

    /**
     * @brief Create a classic-mode EIGRP Autonomous System instance.
     *
     * @param id Numeric AS number.
     * @return Pointer to new EIGRP AS instance, or nullptr if AS already exists.
     *
     * The AS object contains:
     * - IPv4 EIGRP instance (optional)
     * - IPv6 EIGRP instance (optional)
     * - Metrics, K-values, timers, bandwidth/delay policies
     */
    routing::eigrp::EigrpAutonomousSystem* addEigrpAutonomousSystem(uint16_t id);

    /**
     * @brief Look up an existing EIGRP Autonomous System by number.
     *
     * @param id AS number.
     * @return Pointer to AS instance or nullptr if not found.
     */
    routing::eigrp::EigrpAutonomousSystem* getEigrpAutonomousSystem(uint16_t id);

    /**
     * @brief Remove and delete an EIGRP Autonomous System.
     *
     * Any attached EIGRP IPv4 or IPv6 instances inside the AS are deleted as part
     * of the cleanup.
     *
     * @param id AS number to remove.
     * @return True if removed, false if missing.
     */
    bool removeEigrpAutonomousSystem(uint16_t id);

    // EIGRP NAMED SYSTEMS

    /**
     * @brief Create a Named-mode EIGRP configuration group.
     *
     * Named EIGRP (new Cisco-style configuration model) allows IPv4 and IPv6
     * processes under a single hierarchical name.
     *
     * @param name The EIGRP instance name.
     * @return Pointer to the newly created named instance, or nullptr if name exists.
     */
    routing::eigrp::EigrpNamed& addEigrpNamed(const std::string& name);

    /**
     * @brief Retrieve a named EIGRP instance.
     *
     * @param name The named EIGRP configuration identifier.
     * @return Pointer to instance or nullptr if missing.
     */
    routing::eigrp::EigrpNamed* getEigrpNamed(const std::string& name);

    /**
     * @brief Remove a named EIGRP configuration.
     *
     * This operation may also:
     * - Remove IPv4 or IPv6 EIGRP instances associated with the name
     * - Remove the underlying Autonomous System if both AFs are now empty
     *
     * Ensures consistency between:
     * - Named EIGRP trees
     * - Legacy EIGRP AS structures
     *
     * @param name EIGRP name to delete.
     * @return True if removed, false otherwise.
     */
    bool removeEigrpNamed(const std::string& name);

    // OSPF PROCESS
    
    /**
     * @brief Synchronizes all OSPF (v2 and v3) processes with the current interface state.
     *
     * Called when interfaces are added, removed, or reconfigured. Re-evaluates
     * which interfaces are eligible for OSPF participation based on configured
     * areas and address families, then updates DR/BDR elections and adjacencies
     * as needed.
     */
    void refreshOspf();

    /**
     * @brief Creates a OSPFv2 instance.
     *
     * OSPFv2 allows a IPv4 process under a single process ID.
     *
     * @param id Process ID.
     * @return Reference to the newly created OSPFv2 instance.
     */
    routing::ospf::OspfProcess& addOspf(uint16_t id);

    /**
     * @brief retreives an ospfv2 instance.
     *
     * @param id process id.
     * @return pointer to instance or nullptr if missing.
     */
    routing::ospf::OspfProcess* getOspf(uint16_t id);

    /**
     * @brief Remove and delete an OSPFv2 process.
     *
     * @param id Process ID to remove.
     * @return True if removed, false if missing.
     */
    bool removeOspf(uint16_t id);

    // OSPFv3 PROCESS

    /**
     * @brief Synchronizes all OSPFv3 processes with the current interface state.
     *
     * Called when interfaces are added, removed, or reconfigured. Re-evaluates
     * which interfaces are eligible for OSPFv3 participation based on configured
     * areas and address families (IPv4 and IPv6 under a single process ID), then
     * updates DR/BDR elections and adjacencies as needed.
     */
    void refreshOspfv3();

    /**
     * @brief Creates an OSPFv3 process.
     *
     * @param id Process ID.
     * @param af Address family this process services.
     * @return Reference to the newly created OSPFv3 process.
     */
    routing::ospf::OspfProcess& addOspfv3(uint16_t id, types::AddressFamily af);

    /**
     * @brief Retreives an OSPFv3 process.
     *
     * @param id Process ID.
     * @return Pointer to the process or nullptr if missing.
     */
    routing::ospf::OspfProcess* getOspfv3(uint16_t id);

    /**
     * @brief Remove and delete an OSPFv3 process.
     *
     * @param id Process ID to remove.
     * @return True if removed, false if missing.
     */
    bool removeOspfv3(uint16_t id);

    // GLOBAL HELPERS

    /**
     * @brief Returns the per-VRF interface manager.
     *
     * The @ref interface::InterfaceManager tracks which interfaces belong to this
     * VRF and exposes iteration and lookup over that set.
     */
    interface::InterfaceManager& getInterfaceManager() { return ifaceMgr; }

    /**
     * @brief Returns the per-vrf configuation registry.
     *
     * The @ref config::VrfRegistry holds all configs belonging to this VRF.
     */
    config::VrfRegistry& getConfigs();

    /**
     * @brief Returns the global configuation registry.
     *
     * The @ref config::GlobalRegistry holds all configs belonging to the global scope..
     */
    config::GlobalRegistry& getGlobalConfigs();

    std::string getName() { return instanceName; }
    uint32_t getInstanceId() { return instanceId; }
    bool isDefault() { return defaulted; }
    Global& getGlobal() { return global; }

    /**
     * @brief Returns the per-VRF Routing Information Base.
     *
     * Protocols install and withdraw routes here. The RIB in turn updates the
     * FIB and notifies any registered @ref RouteWatcher observers.
     */
    RoutingTable& getRib() { return routingTable; }

    /** @brief Returns a const view of the per-VRF RIB for read-only consumers. */
    const RoutingTable& getRib() const { return routingTable; }

    /**
     * @brief Returns the per-VRF TCP connection manager.
     *
     * Used by BGP and other TCP-based protocols to open and accept connections
     * scoped to this VRF's address space.
     */
    transport::tcp::Tcp& getTcp() { return tcpManager; }

    /**
     * @brief Returns the global @ref ControlScheduler shared by all VRFs.
     *
     * Protocols use this to obtain @ref ProcessQueue instances for serialized,
     * timer-aware task scheduling.
     */
    ControlScheduler& getControlScheduler();
    
private:
    friend class interface::Interface;
    uint32_t instanceId{0};
    const bool defaulted{false}; ///< True for the single "default" VRF that cannot be deleted.

    config::VrfRegistry& configs; ///< Tracks all VRF related configs.

    interface::InterfaceManager ifaceMgr; ///< Tracks interfaces attached to this VRF.

    transport::tcp::Tcp tcpManager; ///< Per-VRF TCP stack for BGP and other transport protocols.

    std::unordered_map<uint32_t, routing::eigrp::EigrpAutonomousSystem> eigrpList; ///< Classic-mode EIGRP AS containers. Keyed by AS number.
    std::unordered_map<std::string, routing::eigrp::EigrpNamed> namedEigrpList; ///< Named-mode EIGRP groups. Keyed by instance name.

    std::unordered_map<uint32_t, routing::ospf::OspfProcess> ospfList; ///< OSPFv2 process instances. Keyed by process ID.
    std::unordered_map<uint32_t, routing::ospf::OspfProcess> ospfv3List; ///< OSPFv3 instances. Keyed by process ID.

    std::string instanceName; ///< Human-readable VRF identifier.

    RoutingTable routingTable; ///< Per-VRF Routing Table (RIB + FIB generation logic).
    Global& global; ///< Reference to global system controller.
};

} // namespace core

#endif // VIRTUAL_ROUTER_H

