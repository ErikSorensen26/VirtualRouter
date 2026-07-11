/**
 * @file ExternalRouteManager.h
 * @brief Derives external routes from AS-External LSAs in the process-wide external LSDB.
 */

#ifndef OSPF_EXTERNAL_ROUTE_MANAGER_H
#define OSPF_EXTERNAL_ROUTE_MANAGER_H

#include <optional>
#include <utility>
#include "ospf/database/LsdbTypes.hpp"

namespace types { struct IPPrefix; }

namespace routing::ospf
{
struct OspfPath;
class OspfProcess;

/**
 * @brief Derives external routes from Type-5 LSAs (RFC 2328 §16.4).
 * @ingroup OSPF
 *
 * Process-scoped, one instance per @ref OspfProcess, since external LSAs are
 * AS-scoped rather than per-area.  Pure computation — resolves each LSA's
 * forwarding address or advertising ASBR and produces `(prefix, OspfPath)`
 * pairs; it never mutates the external LSDB or originates LSAs.  Intra- and
 * inter-area derivation live in @ref IntraRouteManager and
 * @ref InterRouteManager.
 *
 * All methods run on the owning process's single-threaded `ProcessQueue`.
 */
class ExternalRouteManager
{
public:

    /**
     * @brief Binds the route manager to its owning process.
     *
     * @param process The process whose external LSDB and topology table are
     *                consulted; must outlive this object.
     */
    ExternalRouteManager(OspfProcess& process);

    /**
     * @brief Derives a single external (Type-5) route from an LSA record.
     *
     * Resolves the advertising ASBR through the topology table to obtain a
     * forwarding cost and next-hops. Returns nullopt for the path if the ASBR
     * is unreachable or the LSA should be ignored.
     *
     * @tparam Policy  Version policy providing external LSA body accessors.
     * @param key      LSDB key for the Type-5 LSA.
     * @param rec      (header, body) pair for the LSA.
     * @return (prefix, optional path) — nullopt path means the route should be withdrawn.
     */
    template<typename Policy>
    std::pair<types::IPPrefix, std::optional<OspfPath>> deriveExternalRoute(const LsaKey& key, const std::pair<LsaHeader, LsaBody>& rec);

    /**
     * @brief Recomputes all external routes for the process from the current LSDB state.
     *
     * Iterates every Type-5 LSA in the process-wide LSDB and calls
     * `deriveExternalRoute` for each, returning the full set of valid paths.
     *
     * @tparam Policy  Version policy providing external LSA body accessors.
     * @return Complete set of (prefix, path) pairs for all reachable externals.
     */
    template<typename Policy>
    std::vector<std::pair<types::IPPrefix, OspfPath>> deriveExternalRoutes();

private:
    OspfProcess& process;
};
}

#endif // OSPF_EXTERNAL_ROUTE_MANAGER_H
