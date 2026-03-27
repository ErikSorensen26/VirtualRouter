/**
 * @file NeighborTable.h
 * @brief OSPF neighbor table: per-interface peer registry and unicast configuration.
 */

#ifndef OSPF_NEIGHBOR_TABLE_H
#define OSPF_NEIGHBOR_TABLE_H

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace types { struct IPAddress; }

namespace routing::ospf
{
class OspfInterface;
class Neighbor;

/**
 * @brief Maintains the set of OSPF neighbors discovered on one interface.
 * @ingroup OSPF_NEIGHBOR
 *
 * `NeighborTable` is the authoritative store of `Neighbor` objects for a
 * single `OspfInterface`. It provides creation, deletion, and lookup by
 * Router ID, as well as the unicast neighbor configuration map used on NBMA
 * and point-to-multipoint segments.
 *
 * Each instance contains:
 * - `neighbors` — map of live `Neighbor` objects keyed by Router ID.
 * - `unicast`   — statically configured unicast peers (address → poll/cost
 *   overrides), used to populate Hello destinations on non-broadcast segments.
 *
 * ## Architectural Role
 * `NeighborTable` sits between `OspfInterface` (the link-level owner) and
 * individual `Neighbor` objects. The flooding, packet dispatcher, and timer
 * subsystems obtain `Neighbor*` pointers from here; they do not hold their
 * own references.
 *
 * ## Lifecycle & Ownership
 * Constructed by `OspfInterface` and destroyed with it. `Neighbor` objects
 * are stored by value in `neighbors`, so they are destroyed in place when
 * `deleteNeighbor` or the destructor runs.
 *
 * @warning `deleteNeighbor` invalidates all outstanding `Neighbor*` pointers
 * to the removed neighbor. Callers must not dereference a neighbor pointer
 * after calling `deleteNeighbor` for that neighbor's RID.
 *
 * @see Neighbor
 * @see OspfInterface
 */
class NeighborTable
{
public:
    /**
     * @brief Constructs the neighbor table for an interface.
     * @ingroup OSPF_NEIGHBOR
     *
     * The table starts empty; neighbors are added as Hellos arrive.
     *
     * @param iface  The owning OSPF interface.
     */
    explicit NeighborTable(OspfInterface& iface);

    /**
     * @brief Reconciles the live neighbor table against the unicast configuration.
     *
     * Adds `Neighbor` entries for any statically configured unicast peers
     * that are not yet in the `neighbors` map (using the Attempt state for
     * NBMA peers). Called when the unicast neighbor list changes.
     */
    void syncUnicast();

    /**
     * @brief Removes all unicast-only neighbors that have no active adjacency.
     *
     * Used when the unicast neighbor list is cleared from configuration.
     */
    void clearUnicast();

    /**
     * @brief Creates a new `Neighbor` entry for the given Router ID and address.
     *
     * If a neighbor with @p rid already exists the existing pointer is
     * returned and no new object is created.
     *
     * @param rid        The neighbor's OSPF Router ID.
     * @param ipAddress  The neighbor's IP address.
     * @param unicast    True for statically configured NBMA unicast neighbors.
     * @return Pointer to the created or pre-existing `Neighbor`.
     */
    Neighbor* createNeighbor(uint32_t rid, const types::IPAddress& ipAddress,
                             bool unicast = false);

    /**
     * @brief Removes and destroys the neighbor identified by @p rid.
     *
     * All in-flight timers are cancelled before the entry is erased.
     *
     * @param rid     Router ID of the neighbor to remove.
     * @param unicast True if this was a unicast neighbor entry.
     *
     * @warning Invalidates any `Neighbor*` previously returned for @p rid.
     */
    void deleteNeighbor(uint32_t rid, bool unicast);

    /**
     * @brief Looks up a neighbor by Router ID.
     *
     * @param rid  The Router ID to search for.
     * @return Pointer to the neighbor, or `nullptr` if not found.
     */
    Neighbor* lookup(uint32_t rid);

    /**
     * @brief Looks up a neighbor by Router ID (const overload).
     *
     * @param rid  The Router ID to search for.
     * @return Const pointer to the neighbor, or `nullptr` if not found.
     */
    const Neighbor* lookup(uint32_t rid) const;

    /**
     * @brief Cancels the inactivity timers for all neighbors that are not
     *        yet in the Full state.
     *
     * Called during graceful interface shutdown to prevent timer callbacks
     * from firing after the interface is de-allocated.
     */
    void cancelAllInactiveTimers();

    /**
     * @brief Serialises the neighbor list into the Hello packet body.
     *
     * Writes Router IDs of all known neighbors (regardless of state) into
     * @p buf as required by RFC 2328 §A.3.2 / RFC 5340 §A.3.2.
     *
     * @param[out] buf      Destination buffer.
     * @param      maxSize  Maximum bytes available in @p buf.
     * @return Number of bytes written, or `std::nullopt` if the buffer is
     *         too small to hold the full neighbor list.
     */
    std::optional<size_t> addNeighborList(uint8_t* buf, size_t maxSize);

    std::unordered_map<uint32_t, Neighbor> neighbors; ///< Live neighbors keyed by Router ID.

    /**
     * @brief Per-address configuration for a statically defined unicast neighbor.
     *
     * Used on NBMA and point-to-multipoint segments where neighbors cannot be
     * auto-discovered by multicast Hello.
     */
    struct UnicastConfigs
    {
        std::optional<uint16_t> cost{std::nullopt}; ///< Cost override for this neighbor; nullopt uses the interface cost.
        bool databaseFilter{false};                 ///< When true, LSAs are not flooded to this neighbor.
        uint16_t pollInterval{120};                 ///< Hello poll interval (seconds) used when the neighbor is in Down/Attempt.
        uint8_t priority{0};                        ///< Router priority to advertise to this neighbor.
    };

private:
    std::unordered_map<types::IPAddress, UnicastConfigs> unicast; ///< Statically configured unicast neighbors.
    OspfInterface& iface;
};

} // namespace routing::ospf

#endif // OSPF_NEIGHBOR_TABLE_H
