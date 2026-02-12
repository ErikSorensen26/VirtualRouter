// VirtualRouter.h

#ifndef VIRTUAL_ROUTER_H
#define VIRTUAL_ROUTER_H

#include <string>
#include <set>
#include <shared_mutex>
#include <AddressFamily.hpp>

#include "configs/Registry.hpp"
#include "routing/RoutingTable.hpp"

class Interface; ///< Forward declaration of Interface.
class Global;    ///< Forward declaration of Global.
class ControlScheduler;
namespace Eigrp
{
    struct EigrpAutonomousSystem; ///< Forward declaration of Eigrp Autonomous System.
    struct EigrpNamed;            ///< Forward declaration of Eigrp Named.
}
namespace OSPF
{
    class OspfProcess;
    class OspfV3Instance;
}
enum class InterfaceType: uint8_t; ///< Forward declaration of InterfaceType.

/**
 * @class VirtualRouter
 * @brief Represents a complete routing instance (VRF) within the virtual router.
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
    std::set<AddressFamily> enabledAddressFamilies;

    // INTERFACE MANAGEMENT

    /**
     * @brief Attach an existing Interface to this VRF.
     *
     * Inserts the interface into the VRF's interface map, allowing routing protocols
     * and packet processing engines operating inside the VRF to discover and use it.
     *
     * @param interface Pointer to the interface object.
     * @param key       Globally unique interface key (derived from type + ID).
     *
     * @return The inserted interface pointer, or nullptr if the key already exists.
     *
     * @note Interface ownership stays in the Global controller.
     * @thread_safety Uses a shared lock internally.
     */
    Interface* addInterface(Interface* interface, uint32_t key);

    /**
     * @brief Retrieve an interface from the VRF by its key.
     *
     * @param key Unique interface key.
     * @return The Interface pointer, or nullptr if not present in this VRF.
     *
     * @thread_safety Shared read lock.
     */
    Interface* getInterface(uint32_t key);

    /**
     * @brief Obtain a snapshot copy of the interface list.
     *
     * @return A copy of the unordered_map of interface keys to Interface pointers.
     *
     * The returned map is safe for iteration without holding the lock, but may not
     * reflect subsequent modifications.
     *
     * @thread_safety Uses shared read lock.
     */
    std::unordered_map<uint32_t, Interface*> getinterfaceList();

    /**
     * @brief Remove an interface from this VRF's interface table.
     *
     * Does NOT delete the actual interface; ownership stays global.
     *
     * @param key Interface key to remove.
     * @return True if removed, false if not found.
     *
     * @thread_safety Shared read lock.
     */
    bool removeInterface(uint32_t key);

    // EIGRP AUTONOMOUS SYSTEMS

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
    Eigrp::EigrpAutonomousSystem* addEigrpAutonomousSystem(uint32_t id);

    /**
     * @brief Look up an existing EIGRP Autonomous System by number.
     *
     * @param id AS number.
     * @return Pointer to AS instance or nullptr if not found.
     */
    Eigrp::EigrpAutonomousSystem* getEigrpAutonomousSystem(uint32_t id);

    /**
     * @brief Remove and delete an EIGRP Autonomous System.
     *
     * Any attached EIGRP IPv4 or IPv6 instances inside the AS are deleted as part
     * of the cleanup.
     *
     * @param id AS number to remove.
     * @return True if removed, false if missing.
     */
    bool removeEigrpAutonomousSystem(uint32_t id);

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
    Eigrp::EigrpNamed& addEigrpNamed(const std::string& name);

    /**
     * @brief Retrieve a named EIGRP instance.
     *
     * @param name The named EIGRP configuration identifier.
     * @return Pointer to instance or nullptr if missing.
     */
    Eigrp::EigrpNamed* getEigrpNamed(const std::string& name);

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
     * @brief Creates a OSPFv2 instance.
     *
     * OSPFv2 allows a IPv4 process under a single process ID.
     *
     * @param id Process ID.
     * @return Reference to the newly created OSPFv2 instance.
     */
    OSPF::OspfProcess& addOspf(uint16_t id);

    /**
     * @brief retreives an ospfv2 instance.
     *
     * @param id process id.
     * @return pointer to instance or nullptr if missing.
     */
    OSPF::OspfProcess* getOspf(uint16_t id);

    /**
     * @brief Remove and delete an OSPFv2 process.
     *
     * @param id Process ID to remove.
     * @return True if removed, false if missing.
     */
    bool removeOspf(uint16_t id);

    // OSPFv3 PROCESS

    /**
     * @brief Creates a OSPFv3 instance.
     *
     * OSPFv3 allows IPv4 and IPv6 processes under a single process ID.
     *
     * @param id Process ID.
     * @return Reference to the newly created OSPFv2 instance.
     */
    OSPF::OspfV3Instance& addOspfv3(uint16_t id);

    /**
     * @brief Creates a OSPFv3 address family instance.
     *
     * Creates 1 address family running either ipv4 or ipv6.
     *
     * @param id Process ID.
     * @param af Address Family.
     * @return Reference to the newly created OSPFv2 instance.
     */
    OSPF::OspfProcess& addOspfv3(uint16_t id, AddressFamily af);

    /**
     * @brief Retreives an OSPFv3 instance.
     *
     * @param id Process ID.
     * @return Pointer to instance or nullptr if missing.
     */
    OSPF::OspfV3Instance* getOspfv3(uint16_t id);

    /**
     * @brief Remove and delete an OSPFv3 process.
     *
     * @param id Process ID to remove.
     * @return True if removed, false if missing.
     */
    bool removeOspfv3(uint16_t id);

    /**
     * @brief Remove and delete a OSPFv3 address family.
     *
     * @param id Process ID to remove.
     * @param af Address Family.
     * @return True if removed, false if missing.
     */
    bool removeOspfv3(uint16_t id, AddressFamily af);

    std::shared_mutex interfaceMutex; ///< Protects interfaceList.
    std::unordered_map<uint32_t, Interface*> interfaceList; ///< Interfaces belonging to this VRF.

    // GLOBAL HELPERS
    Config::Registry& getRegistry();
    std::string getName() { return instanceName; }
    uint32_t getInstanceId() { return instanceId; }
    bool isDefault() { return defaulted; }
    Global& getGlobal() { return global; }
    RoutingTable& getRib() { return routingTable; }
    ControlScheduler& getControlScheduler();
    
private:
    friend class Interface;
    uint32_t instanceId{0};
    const bool defaulted{false};

    std::unordered_map<uint32_t, Eigrp::EigrpAutonomousSystem> eigrpList; ///< Classic-mode EIGRP AS containers.
    std::unordered_map<std::string, Eigrp::EigrpNamed> namedEigrpList; ///< Named-mode EIGRP groups.

    std::unordered_map<uint32_t, OSPF::OspfProcess> ospfList;
    std::unordered_map<uint32_t, OSPF::OspfV3Instance> ospfv3List;

    std::string instanceName; ///< Human-readable VRF identifier.

    RoutingTable routingTable; ///< Per-VRF Routing Table (RIB + FIB generation logic).
    Global& global; ///< Reference to global system controller.
};

#endif // VIRTUAL_ROUTER_H
