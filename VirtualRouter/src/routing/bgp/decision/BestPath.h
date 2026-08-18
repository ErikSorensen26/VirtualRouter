/**
 * @file BestPath.h
 * @brief BGP best path selection and route ranking.
 */

#ifndef BGP_BEST_PATH_H
#define BGP_BEST_PATH_H

#include "bgp/rib/RibTypes.hpp"

namespace routing::bgp
{
class BgpScope;

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
    bool medConfed         = false; ///< Compare MED across confederation sub-ASes (bgp bestpath med confed).
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
 * Thread-safe only if BgpScope is not modified concurrently; intended for
 * use from the BgpScope scheduler thread only.
 *
 * @see BgpScope, DecisionEngine
 */
class BestPathComparator
{
public:
    /**
     * @brief Constructs a best path comparator for the given BGP scope.
     *
     * @param s BGP scope that owns this comparator.
     * @param cfg Configuration for best path tie-breaking (optional).
     */
    explicit BestPathComparator(BgpScope& s, BestPathConfig cfg = {});

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
     * the medMissingAsWorst configuration flag. Unless `always-compare-med` is
     * configured, MED is only compared between routes from the same neighbouring
     * AS (RFC 4271 9.1.2.2 (c)).
     *
     * @param lhsRoute Left-hand route.
     * @param rhsRoute Right-hand route.
     * @return True if lhs is better.
     */
    inline bool compareMed(const InboundRouteBase& lhsRoute, const InboundRouteBase& rhsRoute) const;

    /**
     * @brief Returns true when both routes were learned from the same neighbouring AS.
     *
     * Uses the leftmost AS of each AS_PATH, falling back to the session peer AS when
     * the path carries no AS_SEQUENCE.
     *
     * @param medConfed When set, a leading AS_CONFED_SEQUENCE/SET supplies the
     *                  neighbouring AS so MED is comparable across confederation
     *                  sub-ASes (RFC 5065 5.3).
     */
    static bool sameNeighborAs(const InboundRouteBase& lhsRoute, const InboundRouteBase& rhsRoute,
                               bool medConfed);

    BgpScope& scope;          ///< Reference to owning BGP scope.
    BestPathConfig config;      ///< Best path selection configuration.
};
} // namespace routing::bgp

#endif //BGP_BEST_PATH_H

