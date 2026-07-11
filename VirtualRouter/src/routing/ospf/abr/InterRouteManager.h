/**
 * @file InterRouteManager.h
 * @brief Derives inter-area routes and ASBR reachability from Type-3/Type-4 LSAs.
 */

#ifndef OSPF_INTER_ROUTE_MANAGER_H
#define OSPF_INTER_ROUTE_MANAGER_H

#include <optional>
#include <utility>
#include "ospf/database/LsdbTypes.hpp"

namespace types { struct IPPrefix; }

namespace routing::ospf
{
struct OspfPath;
struct SpfResult;
class Area;

/**
 * @brief Derives inter-area routes from summary LSAs (RFC 2328 §16.2).
 * @ingroup OSPF
 *
 * Process-scoped and stateless: one instance per @ref OspfProcess, with the
 * target area passed to each call.  Pure computation — reads the area LSDB
 * and SPF results, produces `(prefix, OspfPath)` pairs and ASBR-reachability
 * updates; it never mutates the LSDB or originates LSAs.  Intra-area and
 * external derivation live in @ref IntraRouteManager and
 * @ref ExternalRouteManager.
 *
 * All methods run on the owning process's single-threaded `ProcessQueue`.
 */
class InterRouteManager
{
public:

    /**
    * @brief Derives a single inter-area network route from a Type-3 (or OSPFv3 equivalent) LSA.
    *
    * Returns the prefix and the derived path, or `nullopt` for the path if the LSA
    * is unreachable (e.g., the advertising ABR is not in the SPF tree).
    *
    * @tparam Policy  Version policy providing inter-area LSA body accessors.
    * @param area     Area in which the Type-3 LSA was received.
    * @param key      LSDB key for the LSA.
    * @param header   Common LSA header.
    * @param body     Parsed LSA body variant.
    * @return (prefix, optional path) — nullopt path means the prefix should be withdrawn.
    */
    template<typename Policy>
    std::pair<types::IPPrefix, std::optional<OspfPath>> deriveInterAreaNetwork(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body);

    /**
    * @brief Processes a Type-4 (ASBR summary) LSA and updates the topology table.
    *
    * Type-4 LSAs do not produce prefix routes; instead they update the ASBR
    * reachability information used when installing external routes.
    *
    * @tparam Policy  Version policy providing ASBR summary body accessors.
    * @param area     Area in which the LSA was received.
    * @param key      LSDB key for the LSA.
    * @param header   Common LSA header.
    * @param body     Parsed LSA body variant.
    */
    template <typename Policy>
    void deriveInterAreaRouter(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body);

    /**
    * @brief Derives all inter-area routes visible from an SPF result.
    *
    * Scans the area's LSDB for Type-3/Type-4 LSAs whose advertising ABR is
    * reachable in `spf` and appends the derived paths to `pathList`.
    *
    * @tparam Policy  Version policy providing inter-area LSA body accessors.
    * @param spf      Completed SPF result (used to verify ABR reachability).
    * @param pathList Output list to which (prefix, path) pairs are appended.
    * @param area     Area providing the LSDB.
    */
    template<typename Policy>
    void deriveInterAreaRoutes(const SpfResult& spf, std::vector<std::pair<types::IPPrefix, OspfPath>>& pathList, Area& area);
};
}

#endif // OSPF_INTER_ROUTE_MANAGER_H
