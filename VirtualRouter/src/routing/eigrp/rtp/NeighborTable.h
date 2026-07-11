/**
 * @file NeighborTable.h
 * @brief EIGRP neighbor table: peer list and adjacency lifecycle management.
 */

#ifndef EIGRP_NEIGHBOR_TABLE_H
#define EIGRP_NEIGHBOR_TABLE_H

#include <unordered_set>
#include <IPAddress.h>
#include "Neighbor.h"

namespace routing::eigrp
{
class EigrpInterface;

/**
 * @brief Tracks all EIGRP neighbors discovered on a single interface.
 * @ingroup EIGRP_RTP
 *
 * `NeighborTable` is the authoritative registry of active EIGRP adjacencies for
 * one @ref EigrpInterface. It creates and destroys @ref Neighbor objects,
 * mediates adjacency state transitions, and provides lookup by IP address.
 *
 * Each instance holds:
 * - A map of multicast-discovered neighbors keyed by their IP address.
 * - A separate set of unicast neighbor IPs for point-to-point or manually
 *   configured peers that never join the multicast group.
 *
 * ## Architectural Role
 * `NeighborTable` is the boundary between the RTP receive path
 * (@ref ReliableTransport) and the DUAL/topology layer. When a neighbor's hold
 * timer expires or a protocol error forces teardown, `onDown` drives the
 * notification chain up to the topology table and route manager. It does not
 * make routing decisions itself.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref EigrpInterface. Constructed with a reference to the interface
 * for timer management and metric access. Neighbors are created on first HELLO
 * receipt via @ref createNeighbor and removed on hold-timer expiry or explicit
 * protocol events via @ref deleteNeighbor.
 *
 * ## Concurrency Model
 * All methods must be called from the EIGRP interface scheduler thread.
 * No internal locks are held; thread safety is the caller's responsibility.
 *
 * @see Neighbor
 * @see ReliableTransport
 * @see EigrpInterface
 */
class NeighborTable
{
public:
    /**
     * @brief Constructs an empty neighbor table for the given interface.
     *
     * Stores a reference to `iface` for timer scheduling and metric lookups
     * performed during neighbor formation.
     *
     * @param iface Owning EIGRP interface; must outlive this object.
     */
    NeighborTable(EigrpInterface& iface);
    ~NeighborTable();

    /**
     * @brief Creates and registers a new neighbor entry for the given IP address.
     *
     * Allocates a @ref Neighbor in `neighbors` (multicast) or records the IP
     * in `unicast` depending on the `unicast` flag. If an entry for `ipAddress`
     * already exists, the existing pointer is returned without creating a duplicate.
     *
     * @param ipAddress IP address of the new neighbor.
     * @param v         TLV version capability negotiated during HELLO exchange.
     * @param unicast   True if this is a manually configured unicast peer.
     * @return Pointer to the created or existing @ref Neighbor.
     */
    Neighbor* createNeighbor(const types::IPAddress& ipAddress, Neighbor::Version v = Neighbor::Version::UNKNOWN, bool unicast = false);

    /**
     * @brief Removes and destroys the neighbor entry for the given IP address.
     *
     * Cancels any outstanding hold and graceful-restart timers before erasing
     * the entry. The caller must ensure no outstanding reliable packets reference
     * this neighbor before calling this method.
     *
     * @param neighborIp IP address of the neighbor to remove.
     * @param unicast    True if removing a unicast-configured peer.
     */
    void deleteNeighbor(const types::IPAddress& neighborIp, bool unicast);

    /**
     * @brief Looks up a neighbor by IP address.
     *
     * Searches the multicast `neighbors` map only. Use @ref lookupUnicast for
     * unicast-configured peers.
     *
     * @param neighborIp IP address to look up.
     * @return Pointer to the matching @ref Neighbor, or nullptr if not found.
     */
    Neighbor* lookup(const types::IPAddress& neighborIp);

    /**
     * @brief Returns pointers to all unicast-configured neighbors.
     *
     * Iterates the `unicast` IP set and resolves each entry to its @ref Neighbor
     * via the `neighbors` map.
     *
     * @return Vector of pointers to active unicast neighbors.
     */
    std::vector<Neighbor*> lookupUnicast();

    /// Returns the number of multicast neighbors currently in the table.
    size_t size();

    /**
     * @brief Handles a neighbor going down and notifies the protocol stack.
     *
     * Invoked when a neighbor's hold timer expires or an error forces teardown.
     * Transitions the neighbor to @ref Neighbor::State::DOWN, clears its
     * reliable packet queues, and propagates the down event to the topology
     * table so DUAL can remove routes learned via this peer.
     *
     * @param neighbor The neighbor that has gone down.
     */
    void onDown(Neighbor& neighbor);

    /**
     * @brief Triggers a full topology resynchronization with all active neighbors.
     *
     * Called after a local topology change that requires all peers to receive
     * a fresh full UPDATE. Each neighbor's INIT state is reset and
     * @ref ReliableTransport::sendFullTopology is scheduled for each.
     */
    void resync();

    /**
     * @brief Initiates the graceful-restart hold procedure for a single neighbor.
     *
     * Marks the neighbor as gracefully restarting and arms a graceful-restart
     * timer. Routes learned from this neighbor are retained until the timer
     * expires or the neighbor completes re-synchronization.
     *
     * @param neighbor Neighbor entering the graceful-restart window.
     */
    void startGracefulRestart(Neighbor& neighbor);

    /**
     * @brief Cancels the hold timer for every neighbor in the table.
     *
     * Used during interface shutdown to prevent stale timer callbacks from
     * firing after the interface object is destroyed.
     */
    void cancelAllHoldTimers();

    /**
     * @brief Validates that a point-to-point interface has at most one neighbor.
     *
     * On point-to-point links exactly one neighbor is expected. Returns false
     * and logs a warning if a second neighbor is discovered with a different IP.
     *
     * @param neighborIp IP address of the neighbor being validated.
     * @return True if the neighbor is acceptable; false if the PTP constraint is violated.
     */
    bool validatePTP(const types::IPAddress& neighborIp);

    /**
     * @brief Removes all non-unicast (multicast-discovered) neighbors.
     *
     * Called when multicast is disabled on the interface. Each removed neighbor
     * goes through the normal @ref onDown teardown path.
     */
    void removeAllMulticast();

    /**
     * @brief Enables multicast neighbor discovery on the interface.
     *
     * Joins the EIGRP multicast group and allows the creation of new
     * multicast-discovered neighbors from incoming HELLOs.
     */
    void enableMulticast();

    /**
     * @brief Disables multicast neighbor discovery and removes existing multicast neighbors.
     *
     * Leaves the multicast group and calls @ref removeAllMulticast. Only
     * manually configured unicast neighbors remain active after this call.
     */
    void disableMulticast();

    // NEIGHBOR MANAGEMENT

    std::unordered_map<types::IPAddress, Neighbor> neighbors; ///< Multicast-discovered neighbors keyed by IP; the authoritative adjacency list.
    std::unordered_set<types::IPAddress> unicast;   ///< IP addresses of manually configured unicast peers.

    EigrpInterface& iface; ///< Owning interface; used for timer scheduling and metric access.
};
} // namespace routing::eigrp

#endif // EIGRP_NEIGHBOR_TABLE
