/**
 * @file Topology.h
 * @brief Facade combining the DUAL engine and topology table for an EIGRP process.
 */

#ifndef EIGRP_TOPOLOGY_H
#define EIGRP_TOPOLOGY_H

#include <IPAddress.h>

#include "eigrp/topology/DualEngine.h"

class Internal_EigrpTest;

namespace routing::eigrp
{
class EigrpInterface;
class Eigrp;

/**
 * @brief Facade that combines the @ref DualEngine and @ref TopologyTable for
 *        a single EIGRP process.
 * @ingroup EIGRP_CORE
 *
 * `EigrpTopology` is the primary interface through which the @ref Eigrp
 * process and @ref EigrpInterface objects interact with the DUAL convergence
 * engine.  It owns the @ref DualEngine (which owns the @ref TopologyTable and
 * @ref TimerManager) and exposes a set of high-level operations:
 *
 * - Recalculating all routes after a bulk change
 * - Pruning expired topology entries
 * - Injecting and removing connected-route entries
 * - Forwarding SIA timeout events into the DUAL engine
 *
 * ## Architectural Role
 * Acts as the boundary between the `Eigrp` process object and the DUAL
 * algorithm.  The process does not call into `DualEngine` directly; all
 * topology interactions flow through this class.
 *
 * ## Lifecycle & Ownership
 * Owned by and lives inside the @ref Eigrp object.  The `dual` member is
 * constructed first and holds a `base` back-reference; both must be destroyed
 * together.
 *
 * @see DualEngine
 * @see TopologyTable
 */
class EigrpTopology
{
public:
    friend class ::Internal_EigrpTest;
    friend class EigrpInterface;

    /**
     * @brief Constructs the topology facade bound to the given EIGRP process.
     *
     * Initializes the @ref DualEngine and all dependent subsystems.
     *
     * @param base The @ref Eigrp process that owns this topology.
     */
    EigrpTopology(Eigrp& base);

    /**
     * @brief Forces DUAL to recompute successors for every prefix in the
     *        topology table.
     *
     * Used after bulk changes such as process restart or a K-value change
     * that invalidates all existing metrics.
     */
    void recalculateAll();

    /**
     * @brief Removes topology entries whose validity window has expired.
     *
     * Called periodically by the maintenance timer to purge routes learned
     * from neighbors that have gone down and have exceeded the del-timer.
     */
    void pruneStaleRoutes();

    /**
     * @brief Injects a directly connected route from `iface` into the
     *        topology table.
     *
     * Called when an interface becomes active so the connected prefix is
     * visible to DUAL and can be advertised to neighbors.
     *
     * @param iface The newly active EIGRP interface whose connected prefix
     *              should be injected.
     */
    void synchronizeConnected(EigrpInterface& iface);

    /**
     * @brief Withdraws the directly connected route associated with `iface`
     *        from the topology table.
     *
     * Called when an interface goes down so DUAL can re-converge and the
     * prefix is withdrawn from neighbors.
     *
     * @param iface The interface whose connected prefix should be removed.
     */
    void clearConnected(EigrpInterface& iface);

    /**
     * @brief Handles a Stuck-In-Active timeout for the given query and neighbor.
     *
     * Delegates to @ref DualEngine::handleSIATimeout().  If all retries are
     * exhausted the neighbor is declared down.
     *
     * @param query    The @ref OutgoingQuery whose SIA timer fired.
     * @param neighbor The neighbor that has not yet replied.
     */
    void handleSIATimeout(OutgoingQuery& query, Neighbor& neighbor);

    /**
     * @brief Returns a reference to the raw topology entry map.
     *
     * Provided for read-access by the @ref RouteManager and debug tools.
     */
    std::unordered_map<types::IPPrefix, TopologyEntry>& entries();

    Eigrp& getBase() { return base; }

private:

    DualEngine dual; ///< DUAL algorithm engine; owns the TopologyTable and TimerManager.
    Eigrp& base;     ///< Owning EIGRP process.
};
} // namespace routing::eigrp

#endif // EIGRP_TOPOLOGY_H

