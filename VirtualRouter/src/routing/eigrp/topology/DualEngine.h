/**
 * @file DualEngine.h
 * @brief EIGRP DUAL finite state machine: feasibility conditions and recomputation.
 */

#ifndef EIGRP_DUAL_ENGINE_H
#define EIGRP_DUAL_ENGINE_H

#include <cstdint>
#include <IPAddress.h>
#include <unordered_map>
#include <unordered_set>

#include "TopologyTable.h"
#include "TimerManager.h"

class Internal_EigrpTest;

namespace routing::eigrp
{
class EigrpInterface;
class Eigrp;
class Neighbor;

struct ActiveRoute;
struct ReceivedRoute;
struct TopologyEntry;

/**
 * @brief Tracks one outstanding query sent to a specific neighbor during DUAL Active processing.
 * @ingroup EIGRP_TOPOLOGY
 *
 * While a prefix is in the Active state, DUAL sends a Query to each neighbor
 * and waits for a Reply.  One `OutgoingQuery` is created per neighbor per
 * Active prefix.  It carries the SIA timer ID so the watchdog can be
 * cancelled when the Reply arrives, and the SIA sequence number used to
 * correlate SIA-Query / SIA-Reply exchanges.
 */
struct OutgoingQuery
{
    ActiveRoute* route;           ///< Back-pointer to the containing active route record.
    uint32_t siaSequence{0};      ///< Sequence number of the most recent SIA-Query sent to this neighbor.
    uint32_t querySequence{0};    ///< Sequence number of the original Query that started this active cycle.
    uint32_t siaTimerId{0};       ///< Handle used to cancel the SIA watchdog timer.
    uint32_t siaAttempts{0};      ///< Number of SIA-Queries sent without receiving an SIA-Reply.
};

/**
 * @brief Per-prefix state maintained by the DUAL engine while a route is being recomputed.
 * @ingroup EIGRP_TOPOLOGY
 *
 * When no successor exists for a prefix after local recomputation, DUAL
 * transitions the prefix to the Active state and sends Queries to all
 * neighbors.  `ActiveRoute` collects the replies and possible new paths
 * until all outstanding queries are resolved, at which point
 * `DualEngine::concludeActive` is called.
 *
 * `pendingQueries` is keyed by the neighbor's IP address.  A reply from a
 * neighbor removes its entry from the map; when the map is empty all
 * outstanding queries have been resolved.
 */
struct ActiveRoute
{
    types::IPPrefix activePrefix;
    std::unordered_map<types::IPAddress, OutgoingQuery> pendingQueries; ///< Neighbors that have not yet replied to the active query.
    std::vector<std::pair<types::IPAddress, ReceivedRoute>> possibleRoutes; ///< Candidate routes received during the active cycle.
    struct PairHash {
        size_t operator()(const std::pair<types::IPAddress, uint32_t>& p) const noexcept {
            size_t h = std::hash<types::IPAddress>{}(p.first);
            h ^= std::hash<uint32_t>{}(p.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_set<std::pair<types::IPAddress, uint32_t>, PairHash> remoteSources; ///< (neighbor IP, sequence) pairs from which query propagation is expected.
    RouteInfo* originRoute = nullptr;          ///< The route record that triggered this active cycle, if known.
    types::IPAddress originNeighbor;           ///< The neighbor from whom the loss event was received.
};

/**
 * @brief DUAL (Diffusing Update ALgorithm) engine for the EIGRP routing process.
 * @ingroup EIGRP_TOPOLOGY
 *
 * `DualEngine` implements the DUAL finite state machine described in RFC 7868
 * and the original Cisco EIGRP specification.  It is the authoritative
 * component for all feasibility decisions, successor selection, and
 * Active-state query management within one EIGRP process.
 *
 * Responsibilities include:
 * - Evaluating the feasibility condition for every received route and
 *   classifying neighbors as successors, feasible successors, or neither.
 * - Transitioning prefixes between Passive (stable) and Active (recomputing)
 *   states and managing the full Active lifecycle.
 * - Detecting and recovering from the Stuck-In-Active (SIA) condition when
 *   a neighbor does not reply to a query within the active-time window.
 * - Applying per-interface summary suppression so more-specific prefixes are
 *   not advertised when a covering summary is installed.
 *
 * ## Architectural Role
 * `DualEngine` owns `topologyTable` and `tmgr` (timer manager).  It is
 * owned by and co-located with the `Eigrp` process object.  Per-interface
 * route events arrive via @ref TopologyController, which translates
 * interface-scoped neighbor events into the process-wide DUAL method calls
 * defined here.  After recomputation, DUAL instructs the process to update
 * the RIB via `Eigrp`.
 *
 * ## Lifecycle & Ownership
 * Created at EIGRP process start.  `activeRoutes` is the only dynamically
 * sized state; it is empty in steady state and grows only when prefixes are
 * in the Active state.
 *
 * ## Concurrency Model
 * All methods are called on the EIGRP process scheduler thread.  No internal
 * locking is used; callers are responsible for ensuring single-threaded
 * access.
 *
 * @warning Do not call any `DualEngine` method from outside the EIGRP process
 *          scheduler thread; doing so will corrupt the topology table and
 *          active-route state without any locking to detect it.
 *
 * @see TopologyTable
 * @see TimerManager
 * @see TopologyController
 */
class DualEngine
{
public:
    friend class ::Internal_EigrpTest;

    /**
     * @brief Constructs the DUAL engine for the given EIGRP process.
     *
     * Initializes an empty topology table and timer manager.  No routes are
     * installed and no timers are armed at construction time.
     *
     * @param process The owning EIGRP process.
     */
    DualEngine(Eigrp& process);

    /**
     * @brief Checks whether a prefix is currently being advertised by this process.
     *
     * Used to suppress re-advertisement of routes that originate locally.
     *
     * @param network Pointer to the raw network address bytes.
     * @param mask    Prefix length in bits.
     * @return True if the prefix is present in the topology table with a
     *         valid (non-withdrawn) successor route.
     */
    bool isRouteAdvertised(const uint8_t* network, uint8_t mask);

    /**
     * @brief Installs or updates summary suppression for a topology entry on an interface.
     *
     * Called by @ref RouteAggregator when a summary route is installed.  Marks
     * the entry as suppressed on the given interface so it is excluded from
     * Update advertisements sent out that interface.
     *
     * @param entry  The topology entry to suppress.
     * @param intKey Hardware key of the interface on which suppression applies.
     * @return True if suppression was newly applied, false if it was already set.
     */
    bool setSuppression(TopologyEntry* entry, uint32_t intKey);

    /**
     * @brief Returns the route record of the current best (successor) path for a prefix.
     *
     * @param prefix The destination prefix to look up.
     * @return Pointer to the successor's `RouteInfo`, or nullptr if no
     *         reachable path exists.
     */
    const RouteInfo* findBestRoute(const types::IPPrefix& prefix);

    /**
     * @brief Transitions a set of topology entries to the Active state.
     *
     * For each entry, DUAL sends Query packets to all neighbors and arms the
     * SIA watchdog timer.  `seq`, if provided, is the sequence number that
     * triggered the transition (used to correlate replies).
     *
     * @param entries  Topology entries to transition to Active.
     * @param seq      Optional sequence number from an incoming Query that
     *                 triggered this active cycle; nullptr for locally triggered events.
     */
    void setActive(std::vector<TopologyEntry*>& entries, const uint32_t* seq = nullptr);

    /**
     * @brief Concludes an Active cycle after all outstanding queries have been resolved.
     *
     * Selects the best route from `route.possibleRoutes`, updates successors,
     * installs the result in the RIB, and transitions the prefix back to the
     * Passive state.
     *
     * @param route The active route record whose query cycle has completed.
     */
    void concludeActive(ActiveRoute& route);

    /**
     * @brief Cleans up all active-state records for a neighbor that has gone down.
     *
     * If the neighbor had pending queries, their entries are removed from
     * `activeRoutes` and the active cycles are re-evaluated.  If the neighbor
     * was the last outstanding query for a prefix, `concludeActive` is called
     * immediately.
     *
     * @param neighborIp IP address of the neighbor that went down.
     */
    void removeActiveNeighbor(const types::IPAddress& neighborIp);

    /**
     * @brief Handles an SIA timeout for a query that has not been replied to.
     *
     * Tears down the neighbor as unresponsive and cleans up the associated
     * active-state records.  If the neighbor was the only outstanding query
     * for a prefix, the active cycle is concluded without that neighbor's input.
     *
     * @param query    The outgoing query record whose SIA timer fired.
     * @param neighbor The neighbor that failed to reply.
     */
    void handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor);

    /**
     * @brief Processes an SIA-Reply received from a neighbor.
     *
     * Cancels the SIA timer for the corresponding query and records that the
     * neighbor is still reachable.  A new SIA timer may be re-armed if the
     * active cycle has not yet concluded.
     *
     * @param neighbor The neighbor that sent the SIA-Reply.
     * @param seqNum   Sequence number matching the original Query.
     */
    void processSIAReply(Neighbor& neighbor, uint32_t seqNum);

    /**
     * @brief Processes a batch of Update routes received from a neighbor.
     *
     * Updates the topology table and triggers feasibility evaluation and
     * successor selection for all affected prefixes.
     *
     * @param newRoutes Routes from the incoming Update packet.
     * @param neighbor  Neighbor that sent the Update.
     */
    void processReceivedRoutes(std::vector<ReceivedRoute>& newRoutes, const Neighbor& neighbor);

    /**
     * @brief Processes Update routes received while the affected prefixes are Active.
     *
     * Routes arriving during an active cycle are buffered in the corresponding
     * `ActiveRoute::possibleRoutes` and considered when the cycle concludes.
     *
     * @param newRoute Update routes received during an active cycle.
     * @param neighbor Neighbor that sent the Update.
     */
    void processReceivedActiveRoutes(std::vector<ReceivedRoute>& newRoute, const Neighbor& neighbor);

    /**
     * @brief Processes Query routes received from a neighbor.
     *
     * For each queried prefix, DUAL determines whether it can reply
     * immediately (a feasible successor exists) or must propagate the query
     * further (going Active itself).  `recvSeq` is stored so that SIA-Reply
     * packets can be correlated back to the originating Query sequence number.
     *
     * @param queriedRoutes Prefixes being queried.
     * @param nbr           Neighbor that sent the Query.
     * @param recvSeq       Sequence number of the received Query packet.
     */
    void processReceivedQueryRoutes(std::vector<ReceivedRoute>& queriedRoutes, Neighbor& nbr, uint32_t recvSeq);

    /**
     * @brief Reselects successors and feasible successors for a set of topology entries.
     *
     * Called after metric changes to update which neighbors qualify as
     * successors and install the best path in the RIB.  Entries for which
     * no successor can be found are transitioned to the Active state.
     *
     * @param entry Topology entries to reevaluate.
     */
    void updateSuccessors(std::vector<TopologyEntry*>& entry);

    /**
     * @brief Refreshes summary suppression flags for a set of entries on a given interface.
     *
     * Called by @ref RouteAggregator after a summary is installed or withdrawn
     * so that the suppression state of covered more-specific prefixes remains
     * consistent.
     *
     * @param entry Topology entries whose suppression state should be refreshed.
     * @param iface The interface on which the summary change occurred.
     */
    void refreshSuppression(std::vector<TopologyEntry*>& entry, EigrpInterface* iface);

    /**
     * @brief Recomputes successor selection for every prefix in the topology table.
     *
     * Called when a process-wide parameter changes (e.g., variance, K-values)
     * that affects path selection across all prefixes.
     */
    void recalculateAllRoutes();

    Eigrp& process;            ///< Owning EIGRP process.
    TopologyTable topologyTable; ///< Process-wide topology table owned by this engine.

private:

    TimerManager tmgr; ///< SIA timer manager; posts callbacks to the process scheduler.

    /**
     * @brief Processes a single Active-state route update from one neighbor.
     *
     * Core helper for `processReceivedActiveRoutes`; updates
     * `ActiveRoute::possibleRoutes` and checks whether the active cycle can
     * be concluded.
     *
     * @param newRoute The single route update to process.
     * @param neighbor The neighbor that sent this route.
     */
    void processReceivedActiveRoute(const ReceivedRoute& newRoute, const Neighbor& neighbor);

    /**
     * @brief Recomputes reported and feasible distances for a topology entry.
     *
     * Updates `bestFD` and the per-source `isFeasibleSuccessor` flags.
     *
     * @param entry       The topology entry to recompute.
     * @param localMetric The local composite metric contribution from this router.
     * @return True if any distance values changed.
     */
    bool recalculateDistances(TopologyEntry* entry, uint64_t localMetric);

    /**
     * @brief Updates the successor and feasible-successor lists for a topology entry.
     *
     * Selects the lowest-FD path as the successor and identifies all paths
     * that satisfy the feasibility condition as feasible successors.
     *
     * @param entry The topology entry to update.
     * @return True if the successor set changed and a RIB update is needed.
     */
    bool recalculateSuccessors(TopologyEntry* entry);

    std::unordered_map<types::IPPrefix, ActiveRoute> activeRoutes; ///< Per-prefix active-state records; non-empty only during recomputation.
};
} // namespace routing::eigrp

#endif // DUAL_ENGINE_H
