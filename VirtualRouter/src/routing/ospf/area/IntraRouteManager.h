/**
 * @file IntraRouteManager.h
 * @brief Derives one area's intra-area prefix routes from SPF results.
 */

#ifndef INTRA_ROUTE_MANAGER_H
#define INTRA_ROUTE_MANAGER_H

#include <vector>

namespace types { struct IPPrefix; }

namespace routing::ospf
{
struct SpfResult;
struct OspfPath;
class Area;

/**
 * @brief Derives intra-area prefix routes for one area from its SPF results.
 * @ingroup OSPF_AREA
 *
 * Owned by @ref Area, one instance per area.  Pure computation — reads the
 * area's LSDB and the SPF tree, appends `(prefix, OspfPath)` pairs; it never
 * mutates the LSDB or the RIB itself.  Inter-area and external route
 * derivation are process-scoped; see @ref InterRouteManager and
 * @ref ExternalRouteManager.
 *
 * All methods run on the owning process's single-threaded `ProcessQueue`.
 */
class IntraRouteManager
{
public:

    /**
     * @brief Binds the route manager to its owning area.
     *
     * @param area The area whose LSDB and SPF results are consulted; must
     *             outlive this object.
     */
    IntraRouteManager(Area& area);

    /**
     * @brief Derives all intra-area (prefix) routes from an SPF result.
     *
     * Walks the settled prefix vertices in `spf` and builds an @ref OspfPath for
     * each reachable network prefix. Next-hops are resolved from the SPF tree.
     *
     * @tparam Policy  Version policy providing the Router/Network LSA body types.
     * @param spf      Completed SPF result for the area.
     * @param pathList Output list to which (prefix, path) pairs are appended.
     */
    template<typename Policy>
    void deriveIntraAreaRoutes(const SpfResult& spf, std::vector<std::pair<types::IPPrefix, OspfPath>>& pathList);

private:
    Area& area;
};
}

#endif // INTRA_ROUTE_MANAGER_H
