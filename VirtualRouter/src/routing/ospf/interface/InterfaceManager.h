/**
 * @file InterfaceManager.h
 * @brief OSPF interface registry: creation, removal, and process-wide queries.
 */

#ifndef OSPF_INTERFACE_MANAGER_H
#define OSPF_INTERFACE_MANAGER_H

#include <IPAddress.h>

#include "ospf/interface/InterfaceId.hpp"

namespace interface { class Interface; }
namespace types { struct IPAddress; }

namespace routing::ospf
{
struct OspfInterfaceId;
class InterfaceConfigs;
class OspfProcess;
class OspfInterface;

/**
 * @brief Owns and indexes all OSPF interfaces active within one OSPF process.
 * @ingroup OSPF_INTERFACE
 *
 * `InterfaceManager` is the authoritative registry for `OspfInterface` objects
 * within an `OspfProcess`. It maps the composite `OspfInterfaceId` key (hardware
 * index + area) to the corresponding `OspfInterface` and provides the query
 * helpers that the SPF engine, flooding logic, and packet dispatcher use to
 * locate interfaces by address or area reachability.
 *
 * Each instance contains:
 * - `ospfInterfaceList` — flat map of all live OSPF interfaces.
 *
 * ## Architectural Role
 * `InterfaceManager` sits between the `OspfProcess` (which drives configuration
 * changes) and the individual `OspfInterface` objects (which run the per-link
 * protocol state). It does not own protocol logic itself; it is purely a
 * lifecycle and lookup layer.
 *
 * ## Lifecycle & Ownership
 * Constructed and owned by `OspfProcess`. `createInterface` inserts a new
 * `OspfInterface` into `ospfInterfaceList` in-place; `removeInterface` erases
 * and destroys it. `deactivateAll` shuts down every interface without removing
 * them (used during graceful process stop). `~InterfaceManager` calls
 * `deactivateAll` before the map is destroyed.
 *
 * @warning `removeInterface` must not be called from within a callback that
 * holds a pointer to the interface being removed — doing so invalidates the
 * pointer mid-flight.
 *
 * @see OspfInterface
 * @see OspfProcess
 */
class InterfaceManager
{
public:
    /**
     * @brief Constructs the manager and associates it with an OSPF process.
     *
     * No interfaces are created at construction time; they are added
     * incrementally via `createInterface`.
     *
     * @param process  The owning OSPF process.
     */
    explicit InterfaceManager(OspfProcess& process);

    /**
     * @brief Destroys the manager and deactivates all remaining interfaces.
     *
     * Stops Hello timers and tears down all neighbor adjacencies before the
     * interface map is cleared.
     */
    ~InterfaceManager();

    /**
     * @brief Creates an `OspfInterface` for the given hardware interface and area.
     *
     * Inserts the interface into `ospfInterfaceList` keyed by @p id and returns
     * a reference to the newly constructed object. If an interface with the
     * same key already exists it is replaced.
     *
     * @param interface  The hardware interface to run OSPF on.
     * @param id         Composite key (hardware index + area).
     * @return Reference to the newly created `OspfInterface`.
     */
    OspfInterface& createInterface(interface::Interface& interface, const OspfInterfaceId& id);

    /**
     * @brief Removes and destroys the interface identified by @p id.
     *
     * Stops all timers and tears down any active neighbor adjacencies before
     * erasing the entry from `ospfInterfaceList`.
     *
     * @param id  Key of the interface to remove.
     */
    void removeInterface(const OspfInterfaceId& id);

    /**
     * @brief Reconciles the interface list against current system state.
     *
     * Adds interfaces that are now OSPF-enabled but not yet tracked, and
     * removes interfaces that have been de-configured. Called after bulk
     * configuration changes.
     */
    void refreshInterfaceList();

    /**
     * @brief Stops all interfaces without removing them from the map.
     *
     * Used during graceful OSPF process shutdown: Hello timers and
     * retransmission timers are cancelled, and all neighbors are transitioned
     * to Down, but the interface objects remain allocated for potential restart.
     */
    void deactivateAll();

    /**
     * @brief Synchronises unicast neighbor configurations across all interfaces.
     *
     * Re-reads the unicast neighbor list from config for each interface and
     * reconciles it against the live neighbor table. Called after a global
     * neighbor configuration change.
     */
    void syncNeighbors();

    /**
     * @brief Looks up an OSPF interface by its composite key.
     *
     * @param id  The interface key to search for.
     * @return Pointer to the `OspfInterface`, or `nullptr` if not found.
     */
    OspfInterface* getInterface(const OspfInterfaceId& id);

    /**
     * @brief Looks up an OSPF interface by one of its IP addresses.
     *
     * Searches all tracked interfaces for one whose primary or secondary
     * address matches @p addr. Used by the packet dispatcher to route
     * incoming unicast packets to the correct interface.
     *
     * @param addr  IP address to search for.
     * @return Pointer to the matching `OspfInterface`, or `nullptr`.
     */
    OspfInterface* getInterfaceByAddress(const types::IPAddress& addr);

    /**
     * @brief Returns the IP addresses of all interfaces reachable within an area.
     *
     * Used by the SPF engine to enumerate intra-area transit links when
     * building the shortest-path tree.
     *
     * @param area  OSPF area ID to filter by.
     * @return Vector of interface IP addresses active in the given area.
     */
    std::vector<types::IPAddress> getReachableInterfaces(uint32_t area);

    /**
     * @brief Checks whether a specific interface is active in a given area.
     *
     * @param area  OSPF area ID.
     * @param id    Hardware interface index.
     * @return True if the interface exists, is up, and belongs to @p area.
     */
    bool isInterfaceReachable(uint32_t area, uint32_t id);

    // INTERFACE LIST

    std::unordered_map<OspfInterfaceId, OspfInterface> ospfInterfaceList; ///< All OSPF interfaces keyed by (interfaceId, area).

private:
    OspfProcess& process; ///< The owning OSPF process.
};

} // namespace routing::ospf

#endif // OSPF_INTERFACE_MANAGER_H
