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
#include "routing/RoutingTable.h"
#include "interface/InterfaceManager.h"

namespace config { struct VrfRegistry; struct GlobalRegistry; struct EigrpRegistry; struct OspfRegistry; struct BgpRegistry; }

namespace interface { class Interface; enum class InterfaceType : uint8_t; }

namespace routing::eigrp {
    struct Eigrp;
    struct EigrpAutonomousSystem;
}
namespace routing::ospf {
    class OspfProcess;
    class Ospfv3Instance;
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
 * The VRF owns all of its protocol instances (EIGRP classic and named mode). They
 * interact with `routingTable` for RIB insertion/removal, with attached interfaces
 * for neighbor discovery and hello processing, and with Global configuration for
 * timing and authentication policies. Interface and protocol lifecycle changes are
 * the slow path; route lookups and adjacency reads happen inside the protocol
 * modules themselves.
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
     * @param cfg    Registry slot already created (by the Executor, via
     *               `emplaceBack`) for this VRF; VirtualRouter binds to it
     *               rather than creating its own.
     */
    VirtualRouter(Global& global, const std::string& name, config::VrfRegistry& cfg);

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

    // EIGRP AUTONOMOUS SYSTEM

    /**
     * @brief Creates and registers an EIGRP classic autonomous system for address family @p af.
     * @param reg Registry to bind the new process to.
     * @param id  Autonomous system number.
     * @param af  Address family (IPv4 or IPv6) this instance serves.
     * @return Reference to the newly created process.
     */
    routing::eigrp::Eigrp& addEigrpAutonomousSystem(config::EigrpRegistry& reg, uint16_t id, types::AddressFamily af);

    /**
     * @brief Looks up an existing EIGRP classic autonomous system.
     * @param id Autonomous system number.
     * @param af Address family (IPv4 or IPv6) of the instance to find.
     * @return Pointer to the process, or `nullptr` if none is registered.
     */
    routing::eigrp::Eigrp* getEigrpAutonomousSystem(uint16_t id, types::AddressFamily af);

    /**
     * @brief Tears down and unregisters an EIGRP classic autonomous system.
     * @param id Autonomous system number.
     * @param af Address family (IPv4 or IPv6) of the instance to remove.
     * @return `true` if a matching process was found and removed.
     */
    bool removeEigrpAutonomousSystem(uint16_t id, types::AddressFamily af);

    // OSPF PROCESS

    /**
     * @brief Creates and registers an OSPFv2 process.
     * @param reg Registry to bind the new process to.
     * @param id  OSPF process ID.
     * @return Reference to the newly created process.
     */
    routing::ospf::OspfProcess& addOspf(config::OspfRegistry& reg, uint16_t id);

    /**
     * @brief Looks up an existing OSPFv2 process.
     * @param id OSPF process ID.
     * @return Pointer to the process, or `nullptr` if none is registered.
     */
    routing::ospf::OspfProcess* getOspf(uint16_t id);

    /**
     * @brief Tears down and unregisters an OSPFv2 process.
     * @param id OSPF process ID.
     * @return `true` if a matching process was found and removed.
     */
    bool removeOspf(uint16_t id);

    // OSPFv3 PROCESS

    /**
     * @brief Creates and registers an OSPFv3 process for address family @p af.
     * @param reg Registry to bind the new process to.
     * @param id  OSPF process ID.
     * @param af  Address family (IPv4 or IPv6) this instance serves.
     * @return Reference to the newly created process.
     */
    routing::ospf::OspfProcess& addOspfv3(config::OspfRegistry& reg, uint16_t id, types::AddressFamily af);

    /**
     * @brief Looks up an existing OSPFv3 process.
     * @param id OSPF process ID.
     * @param af Address family (IPv4 or IPv6) of the instance to find.
     * @return Pointer to the process, or `nullptr` if none is registered.
     */
    routing::ospf::OspfProcess* getOspfv3(uint16_t id, types::AddressFamily af);

    /**
     * @brief Tears down and unregisters an OSPFv3 process.
     * @param id OSPF process ID.
     * @param af Address family (IPv4 or IPv6) of the instance to remove.
     * @return `true` if a matching process was found and removed.
     */
    bool removeOspfv3(uint16_t id, types::AddressFamily af);

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

    std::unordered_map<uint32_t, routing::ospf::OspfProcess> ospfList; ///< OSPFv2 process instances. Keyed by process ID.
    std::unordered_map<uint32_t, routing::ospf::Ospfv3Instance> ospfv3List; ///< OSPFv3 instances. Keyed by process ID.

    std::string instanceName; ///< Human-readable VRF identifier.

    RoutingTable routingTable; ///< Per-VRF Routing Table (RIB + FIB generation logic).
    Global& global; ///< Reference to global system controller.
};

} // namespace core

#endif // VIRTUAL_ROUTER_H

