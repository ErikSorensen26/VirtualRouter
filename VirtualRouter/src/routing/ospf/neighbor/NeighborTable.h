/**
 * @file NeighborTable.h
 * @brief OSPF neighbor table: per-interface peer registry and unicast configuration.
 */

#ifndef OSPF_NEIGHBOR_TABLE_H
#define OSPF_NEIGHBOR_TABLE_H

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include "Neighbor.h"

namespace types { struct IPAddress; }

namespace routing::ospf
{
class OspfInterfaceBase;
class InterfaceTimers;
class Neighbor;
struct DrCandidate;
struct FloodInfo;
struct LsaRecordRef;

/**
 * @brief Maintains the set of OSPF neighbors discovered on one interface.
 * @ingroup OSPF_NEIGHBOR
 *
 * `NeighborTable` is the authoritative store of `Neighbor` objects for a
 * single `OspfInterfaceBase`. It provides creation, deletion, and lookup by
 * Router ID, as well as the unicast neighbor configuration map used on NBMA
 * and point-to-multipoint segments.
 *
 * Each instance contains:
 * - `neighbors` — map of live `Neighbor` objects keyed by Router ID.
 * - `unicast`   — statically configured unicast peers (address → poll/cost
 *   overrides), used to populate Hello destinations on non-broadcast segments.
 *
 * ## Architectural Role
 * `NeighborTable` sits between `OspfInterfaceBase` (the link-level owner) and
 * individual `Neighbor` objects. The flooding, packet dispatcher, and timer
 * subsystems obtain `Neighbor*` pointers from here; they do not hold their
 * own references.
 *
 * ## Lifecycle & Ownership
 * Constructed by `OspfInterfaceBase` and destroyed with it. `Neighbor` objects
 * are stored by value in `neighbors`, so they are destroyed in place when
 * `deleteNeighbor` or the destructor runs.
 *
 * @warning `deleteNeighbor` invalidates all outstanding `Neighbor*` pointers
 * to the removed neighbor. Callers must not dereference a neighbor pointer
 * after calling `deleteNeighbor` for that neighbor's RID.
 *
 * @see Neighbor
 * @see OspfInterfaceBase
 */
class NeighborTable
{
public:
    /**
     * @brief Constructs the neighbor table for an interface.
     *
     * The table starts empty; neighbors are added as Hellos arrive.
     *
     * @param iface  The owning OSPF interface.
     */
    explicit NeighborTable(OspfInterfaceBase& iface, InterfaceTimers& tmgr);

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
     * @brief Drops every neighbor back to the Down state.
     *
     * Cancels each neighbor's inactivity timer first so no dead-interval
     * callback fires mid-reset, then runs the Down transition (which tears
     * down the adjacency and clears exchange/retransmission state).  Entries
     * are kept in the table; adjacencies re-form through the normal Hello
     * exchange.  Used during area and process resets.
     */
    void resetNeighbors();

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

    // ITERATION

    /**
     * @brief Invokes `fn(rid, neighbor)` for every neighbor in the table.
     *
     * Iteration wrapper so callers never touch the underlying map.  `fn`
     * must not create or delete neighbors during iteration; collect keys and
     * use @ref deleteNeighbor after the loop instead.
     */
    template <typename Fn>
    void forEach(Fn&& fn);

    /**
     * @brief Const overload of @ref forEach for read-only traversal.
     */
    template <typename Fn>
    void forEach(Fn&& fn) const;


    /**
     * @brief Returns the number of neighbors currently in the table, in any state.
     */
    size_t size() const { return neighbors.size(); }

    // HELPERS

    /**
     * @brief Returns the local Router ID followed by every neighbor's Router ID.
     *
     * Used when originating this segment's Network LSA, whose body must list
     * all routers attached to the network (RFC 2328 §12.4.2) — the DR itself
     * plus each neighbor on the segment.
     */
    std::vector<uint32_t> getNeighborRIDs() const;

private:
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

    std::unordered_map<types::IPAddress, UnicastConfigs> unicast; ///< Statically configured unicast neighbors.
    OspfInterfaceBase& iface;
    InterfaceTimers& tmgr;
};

template <typename Fn>
void NeighborTable::forEach(Fn&& fn)
{
    for (auto& [rid, nbr] : neighbors)
        fn(rid, nbr);
}

template <typename Fn>
void NeighborTable::forEach(Fn&& fn) const
{
    for (const auto& [rid, nbr] : neighbors)
        fn(rid, nbr);
}
} // namespace routing::ospf

#endif // OSPF_NEIGHBOR_TABLE_H
