/**
 * @file InterfaceManager.h
 * @brief OSPF interface registry: creation, removal, and process-wide queries.
 */

#ifndef OSPF_INTERFACE_MANAGER_H
#define OSPF_INTERFACE_MANAGER_H

#include <IPAddress.h>

#include "ospf/interface/InterfaceId.hpp"
#include "ospf/interface/OspfInterface.h"

namespace interface { class Interface; }
namespace types { struct IPAddress; }

namespace routing::ospf
{
class OspfProcess;
class Area;
class OspfInterface;
struct FloodInfo;
struct LsaRecordRef;

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
     * @brief Tears down and restarts the adjacencies on every managed interface.
     *
     * Fans out `OspfInterface::resetNeighbors()` across the whole interface
     * list: each neighbor drops to Down and is rediscovered via the normal
     * Hello exchange.  Called during area and process resets so no adjacency
     * state survives an LSDB flush.
     */
    void resetNeighbors();

    /**
     * @brief Checks whether a specific interface is active in a given area.
     *
     * @param area  OSPF area ID.
     * @param id    Hardware interface index.
     * @return True if the interface exists, is up, and belongs to @p area.
     */
    bool isInterfaceReachable(uint32_t area, uint32_t id);

    // GETTERS

    /**
     * @brief Looks up a const OSPF interface by its composite key.
     *
     * @param id  The interface key to search for.
     * @return Pointer to the `OspfInterface`, or `nullptr` if not found.
     */
    const OspfInterface* getInterface(const OspfInterfaceId& id) const;

    /**
     * @brief Looks up a const OSPF interface by one of its IP addresses.
     *
     * Searches all tracked interfaces for one whose primary or secondary
     * address matches @p addr. Used by the packet dispatcher to route
     * incoming unicast packets to the correct interface.
     *
     * @param addr  IP address to search for.
     * @return Pointer to the matching `OspfInterface`, or `nullptr`.
     */
    const OspfInterface* getInterfaceByAddress(const types::IPAddress& addr) const;

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

    // INTERNAL GETTERS

    /**
     * @brief Returns an interface's version-agnostic configuration registry.
     *
     * Access-mediation helper: `InterfaceManager` is a friend of
     * @ref OspfInterface, so OSPF-internal collaborators that are not can
     * read interface config through the manager instead of each being
     * friended individually.
     *
     * @param iface The interface whose base config is requested.
     */
    const config::OspfInterfaceBaseRegistry& getInterfaceBaseConfigs(const OspfInterface& iface) const;

    /**
     * @brief Returns an interface's version-specific (OSPFv2/OSPFv3) configuration registry.
     *
     * Same friend-mediation pattern as @ref getInterfaceBaseConfigs.
     *
     * @param iface The interface whose config is requested.
     */
    const config::OspfInterfaceRegistry& getInterfaceConfigs(const OspfInterface& iface) const;

    /**
     * @brief Returns an interface's neighbor table for read-only inspection.
     *
     * Same friend-mediation pattern as @ref getInterfaceBaseConfigs; used by
     * the route managers to resolve SPF next hops to neighbor addresses.
     *
     * @param iface The interface whose neighbor table is requested.
     */
    const NeighborTable& getNTable(const OspfInterface& iface) const;

    // UPDATE

    /**
     * @brief Transmits a batch of flood-ready LSAs out of every eligible interface in an area.
     *
     * For each managed interface belonging to @p area (skipping those with
     * `database-filter all out` configured): on broadcast segments the batch
     * is sent once as a multicast reliable LS Update; on all other network
     * types it is sent per-neighbor as unicast.  Reliable delivery
     * (retransmission until acknowledged) is handled by each interface's
     * packet dispatcher.
     *
     * @param area    Area whose interfaces should flood the batch.
     * @param records Batch of (flood reason, LSA record reference) pairs.
     */
    void broadcastLsu(Area& area, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records);

    // DEMAND CIRCUIT / FLOOD REDUCTION

    /**
     * @brief Applies an area-wide Demand-Circuit capability change to every DC-configured interface.
     *
     * Called by `Area::runDCIntegrityScan()` when the area's DC
     * compatibility flips.  For each interface configured for
     * `demand-circuit` or `flood-reduction` whose active state differs from
     * @p enabled: updates the flood-reduction flag, schedules a Hello so
     * neighbors learn the new DC bit, and re-originates the interface's
     * Router LSA contribution with the updated options.
     *
     * @param enabled True if every router in the area still supports DC
     *                operation (RFC 1793); false disables it everywhere.
     */
    void runDCIntegrityScan(bool enabled);

    // ITERATE

    /**
     * @brief Invokes `fn(id, interface)` for every managed interface.
     *
     * Iteration wrapper over the interface map so collaborators can walk all
     * interfaces without touching the container.  `fn` must not create or
     * remove interfaces during iteration.
     */
    template <typename Fn>
    void forEach(Fn&& fn);

    /**
     * @brief Const overload of @ref forEach for read-only traversal.
     */
    template <typename Fn>
    void forEach(Fn&& fn) const;
    

private:

    std::unordered_map<OspfInterfaceId, OspfInterface> ospfInterfaceList; ///< All OSPF interfaces keyed by (interfaceId, area).

    OspfProcess& process; ///< The owning OSPF process.
};

template <typename Fn>
void InterfaceManager::forEach(Fn&& fn)
{
    for (auto& [id, iface] : ospfInterfaceList)
    {
        using ReturnType = std::invoke_result_t<decltype(fn), decltype(id), decltype((iface))>;
        if constexpr (std::is_same_v<ReturnType, bool>)
        {
            if (fn(id, iface))
                break;
        }
        else
        {
            fn(id, iface);
        }
    }
}

template <typename Fn>
void InterfaceManager::forEach(Fn&& fn) const
{
    for (const auto& [id, iface] : ospfInterfaceList)
    {
        using ReturnType = std::invoke_result_t<decltype(fn), decltype(id), decltype((iface))>;
        if constexpr (std::is_same_v<ReturnType, bool>)
        {
            if (fn(id, iface))
                break;
        }
        else
        {
            fn(id, iface);
        }
    }
}

} // namespace routing::ospf

#endif // OSPF_INTERFACE_MANAGER_H
