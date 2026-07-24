/**
 * @file BestPath.h
 * @brief BGP best path selection and route ranking.
 */

#ifndef BGP_BEST_PATH_H
#define BGP_BEST_PATH_H

#include "bgp/rib/RibTypes.hpp"

namespace routing::bgp
{
class BgpProcess;

/**
 * @brief BGP best path selection configuration options.
 * @ingroup BGP_DECISION
 *
 * Configurable parameters that affect how the best path is chosen when
 * multiple routes to the same destination exist from different peers.
 */
struct BestPathConfig
{
    bool compareMed        = false; ///< Compare MED as tiebreaker (bgp bestpath compare-med).
    bool compareRouterId   = false; ///< Compare router IDs as tiebreaker (bgp bestpath compare-routerid).
    bool medMissingAsWorst = false; ///< Treat missing MED as infinity (bgp bestpath med missing-as-worst).
    bool ignoreIgpMetric   = false; ///< Ignore IGP metric in next-hop cost (bgp bestpath igp-metric-ignore).
};

/**
 * @brief BGP best path selection algorithm (RFC 4271 § 9.1.2).
 * @ingroup BGP_DECISION
 *
 * Implements the tie-breaking rules for selecting the best route when multiple
 * paths to the same destination are available:
 * 1. Highest local preference
 * 2. Shortest AS path
 * 3. Lowest origin
 * 4. Lowest MED (if comparable)
 * 5. eBGP over iBGP
 * 6. Lowest IGP metric to next-hop
 * 7. Oldest route (lowest router ID or peer IP)
 *
 * ## Concurrency Model
 * Thread-safe if BgpProcess is not modified concurrently. Intended for use
 * from BgpProcess scheduler thread only.
 *
 * @see BgpProcess, DecisionEngine
 */
class BestPathComparator
{
public:
    /**
     * @brief Constructs a best path comparator for the given BGP process.
     *
     * @param p BGP process that owns this comparator.
     * @param cfg Configuration for best path tie-breaking (optional).
     */
    explicit BestPathComparator(BgpProcess& p, BestPathConfig cfg = {});

    /**
     * @brief Compares two routes and returns true if lhs is better than rhs.
     *
     * Applies RFC 4271 best path selection algorithm to determine which route
     * should win. Both routes must be to the same destination; only attributes
     * and peer information are compared.
     *
     * @param lhsRoute Left-hand route to compare.
     * @param lhsNbr Left-hand route's peer address (for origin comparison).
     * @param rhsRoute Right-hand route to compare.
     * @param rhsNbr Right-hand route's peer address (for origin comparison).
     * @return True if lhs is preferable (should be installed), false otherwise.
     *
     * @see RFC 4271 Section 9.1.2 - Route Selection
     */
    bool better(const InboundRouteBase& lhsRoute, const types::IPAddress& lhsNbr, const InboundRouteBase& rhsRoute, const types::IPAddress& rhsNbr) const;

private:
    /**
     * @brief Compares MED attribute between two routes.
     *
     * Returns true if lhs MED is lower (better) than rhs MED, considering
     * the medMissingAsWorst and ignoreIgpMetric configuration flags.
     *
     * @param lhsRoute Left-hand route.
     * @param rhsRoute Right-hand route.
     * @return True if lhs is better.
     */
    inline bool compareMed(const InboundRouteBase& lhsRoute, const InboundRouteBase& rhsRoute) const;

    BgpProcess& proc;           ///< Reference to owning BGP process.
    BestPathConfig config;      ///< Best path selection configuration.
};
} // namespace routing::bgp

#endif //BGP_BEST_PATH_H

