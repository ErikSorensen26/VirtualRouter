/**
 * @file TopologyController.h
 * @brief EIGRP interface topology state machine and neighbor management.
 */

#ifndef EIGRP_TOPOLOGY_CONTROLLER_H
#define EIGRP_TOPOLOGY_CONTROLLER_H

#include <cstdint>
#include <vector>
#include <IPAddress.h>
#include <unordered_map>

namespace routing::eigrp
{
class EigrpInterface;
class DualEngine;
class Neighbor;
class NeighborTable;

struct RouteInfo;
struct TopologyEntry;
struct ReceivedRoute;

/**
 * @brief Per-interface topology event dispatcher that bridges neighbor events to the DUAL engine.
 * @ingroup EIGRP_INTERFACE
 *
 * `TopologyController` sits between an @ref EigrpInterface and the process-wide
 * @ref DualEngine.  Its responsibilities are:
 * - Accepting raw route vectors from RTP and routing them to the appropriate
 *   DUAL method (update, active, query, SIA-reply).
 * - Filtering the local topology map to produce the set of routes that may be
 *   advertised out of this interface (honoring stub rules, split-horizon, and
 *   summary suppression).
 * - Computing the local composite metric for this interface so DUAL can rank
 *   paths correctly.
 * - Propagating neighbor-down events to DUAL so active routes can be
 *   re-queried or concluded.
 *
 * ## Architectural Role
 * One `TopologyController` exists per `EigrpInterface`.  It holds no topology
 * state of its own — all topology entries live in `DualEngine::topologyTable`.
 * The controller's role is purely to translate interface-scoped events into
 * process-scoped DUAL operations and to apply interface-local filters when
 * building advertisement lists.
 *
 * ## Lifecycle & Ownership
 * Owned by `EigrpInterface`.  Both `ntable` and `dual` references must outlive
 * this object; they are owned by the containing `Eigrp` process.
 *
 * @see DualEngine
 * @see EigrpInterface
 */
class TopologyController
{
public:
    /**
     * @brief Constructs a controller bound to the given interface and DUAL engine.
     *
     * @param ntable Neighbor table for this interface, used during route filtering.
     * @param dual   Process-wide DUAL engine that performs feasibility calculations.
     * @param iface  The interface this controller serves.
     */
    TopologyController(NeighborTable& ntable, DualEngine& dual, EigrpInterface& iface);

    /**
     * @brief Computes the local composite metric for this interface.
     *
     * The returned value is the EIGRP metric a neighbor would use when
     * installing this router as a next-hop.  It combines bandwidth, delay,
     * reliability, load, and MTU according to the process K-values.
     *
     * @return Composite metric for this interface.
     */
    uint64_t getLocalMetric();

    /**
     * @brief Filters a candidate route list down to routes advertisable on this interface.
     *
     * Applies split-horizon, stub advertisement rules, and summary suppression
     * to `routes`, returning only those that may be sent to neighbors on this
     * interface.
     *
     * @param routes Candidate routes to evaluate.
     * @return Subset of `routes` that pass all advertisement filters.
     */
    std::vector<const RouteInfo*> filterAdvertisableRoutes(const std::vector<const RouteInfo*>& routes);

    /**
     * @brief Returns the full set of routes currently advertisable on this interface.
     *
     * Equivalent to calling @ref filterAdvertisableRoutes with every active
     * successor route in the topology table.
     *
     * @return Routes that may be sent in an EIGRP Update on this interface.
     */
    std::vector<const RouteInfo*> getAdvertisableRoutes();

    /**
     * @brief Handles the loss of a neighbor on this interface.
     *
     * Marks all routes learned from `neighbor` as unreachable and notifies
     * DUAL so it can recompute or initiate queries for affected prefixes.
     *
     * @param neighbor The neighbor that went down.
     */
    void onNeighborDown(Neighbor& neighbor);

    /**
     * @brief Returns the per-prefix topology map owned by the DUAL engine.
     *
     * Provided for read access by the interface layer (e.g., when building
     * Update packets).  Modifications should go through DUAL methods, not
     * directly through the returned map.
     *
     * @return Reference to the process-wide topology entry map.
     */
    std::unordered_map<types::IPPrefix, TopologyEntry>& getTopologies();

    /**
     * @brief Recalculates summary suppression state for a set of topology entries.
     *
     * Called after a summary route is installed or withdrawn to update the
     * suppression flags on the more-specific prefixes that fall under it.
     *
     * @param entries Topology entries whose suppression state should be refreshed.
     */
    void refreshSuppression(std::vector<TopologyEntry*>& entries);

    /**
     * @brief Processes a batch of Update routes received from a neighbor.
     *
     * Routes are inserted or updated in the topology table and then handed
     * to the DUAL engine for feasibility evaluation and successor selection.
     *
     * @param routes   Routes carried in the incoming Update packet.
     * @param neighbor Neighbor that sent the Update.
     */
    void processReceivedRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);

    /**
     * @brief Processes Update routes received while a prefix is in the Active state.
     *
     * Routes arriving during DUAL active processing are buffered as possible
     * successors and evaluated once the active query cycle concludes.
     *
     * @param routes   Routes in the incoming Update.
     * @param neighbor Neighbor that sent the Update.
     */
    void processReceivedActiveRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor);

    /**
     * @brief Processes Query routes received from a neighbor.
     *
     * Determines whether this router can reply immediately or must propagate
     * the query further.  `recvSeq` is the sequence number of the Query
     * packet and is used to correlate SIA-Query / SIA-Reply exchanges.
     *
     * @param routes   Prefixes being queried.
     * @param neighbor Neighbor that sent the Query.
     * @param recvSeq  Sequence number of the received Query packet.
     */
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& routes, Neighbor& neighbor, uint32_t recvSeq);

    /**
     * @brief Handles an SIA-Reply received from a neighbor.
     *
     * Resets the SIA timer for the corresponding outstanding query and
     * records that the neighbor is still reachable and processing.
     *
     * @param neighbor Neighbor that sent the SIA-Reply.
     * @param seq      Sequence number matching the original Query.
     */
    void processSIAReply(Neighbor& neighbor, uint32_t seq);

    /**
     * @brief Marks a route from a specific neighbor as unreachable with metric infinity.
     *
     * Updates the route's feasible distance to the poison-reverse sentinel
     * and notifies DUAL to trigger a recomputation for the containing entry.
     *
     * @param route       The route to poison.
     * @param neighborIp  IP address of the neighbor whose route is being poisoned.
     * @param entry       The topology entry that contains `route`.
     */
    void markRouteUnreachable(RouteInfo& route, const types::IPAddress& neighborIp, TopologyEntry& entry);

    /**
     * @brief Looks up a topology entry by prefix without creating one.
     *
     * @param prefix The network prefix to look up.
     * @return Pointer to the existing entry, or nullptr if not present.
     */
    TopologyEntry* findEntry(const types::IPPrefix& prefix);

    /**
     * @brief Returns the topology entry for `prefix`, creating it if absent.
     *
     * @param prefix The network prefix to look up or create.
     * @return Reference to the (possibly newly created) topology entry.
     */
    TopologyEntry& ensure(const types::IPPrefix& prefix);

private:
    NeighborTable& ntable;  ///< Neighbor table for this interface.
    DualEngine& dual;       ///< Process-wide DUAL engine.
    EigrpInterface& iface;  ///< Interface this controller is bound to.
};
} // namespace routing::eigrp

#endif // EIGRP_TOPOLOGY_CONTROLLER_H
