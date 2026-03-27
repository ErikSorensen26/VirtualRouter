/**
 * @file RouteManager.h
 * @brief Policy-templated functions that translate OSPF LSA bodies into installable route paths.
 */

#ifndef OSPF_ROUTE_MANAGER_H
#define OSPF_ROUTE_MANAGER_H

#include <AddressFamily.hpp>

#include "RoutingTable.h"
#include "ospf/spf/SpfTypes.hpp"
#include "ospf/database/LSDB.hpp"

namespace types { struct IPAddress; }

namespace routing::ospf
{
class Area;
class OspfProcess;

/**
 * @brief Stateless functions that derive @ref OspfPath objects from SPF results and LSA bodies.
 * @ingroup OSPF_TOPOLOGY
 *
 * All functions in this namespace are pure transformations: they read SPF output
 * and LSDB records, compute path attributes, and return (prefix, path) pairs ready
 * for insertion into @ref OspfRib. They do not modify the LSDB, topology table, or
 * global RIB directly.
 *
 * Each function is templated on a `Policy` type that supplies the version-specific
 * LSA body accessors (OSPFv2 vs. OSPFv3 differ in how prefix information is encoded).
 */
namespace routemanager
{
    /// Per-vertex next-hop cache populated during SPF to avoid redundant next-hop resolution.
    using NhCache = std::unordered_map<Vertex, std::vector<OspfNextHop>, VertexHash>;

    /**
     * @brief Constructs an @ref OspfPath from its constituent attributes.
     *
     * Convenience factory used by all derive* functions to ensure consistent
     * field ordering and default values.
     *
     * @param areaId        Source area, or nullopt for process-wide external paths.
     * @param options       OSPF options bits (E-bit, etc.) from the originating LSA.
     * @param adminDistance Administrative distance to apply at RIB installation time.
     * @param cost          Total path cost.
     * @param nextHops      ECMP forwarding entries.
     * @param type          Route type (intra, inter, external, NSSA).
     */
    OspfPath makePath(std::optional<uint32_t> areaId, uint8_t options, uint8_t adminDistance, uint64_t cost, std::vector<OspfNextHop> nextHops, OspfRouteType type);

    /**
     * @brief Derives all intra-area (prefix) routes from an SPF result.
     *
     * Walks the settled prefix vertices in `spf` and builds an @ref OspfPath for
     * each reachable network prefix. Next-hops are resolved from the SPF tree.
     *
     * @tparam Policy  Version policy providing `getIntraAreaPrefixes(vertex, lsdb)` and
     *                 related LSA body accessors.
     * @param spf      Completed SPF result for the area.
     * @param pathList Output list to which (prefix, path) pairs are appended.
     * @param area     Area providing the LSDB and config context.
     */
    template<typename Policy>
    void deriveIntraAreaRoutes(const SpfResult& spf, std::vector<std::pair<types::IPPrefix, OspfPath>>& pathList, Area& area);

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

    /**
     * @brief Derives a single external (Type-5) route from an LSA record.
     *
     * Resolves the advertising ASBR through the topology table to obtain a
     * forwarding cost and next-hops. Returns nullopt for the path if the ASBR
     * is unreachable or the LSA should be ignored.
     *
     * @tparam Policy  Version policy providing external LSA body accessors.
     * @param process  OSPF process providing the topology table for ASBR resolution.
     * @param key      LSDB key for the Type-5 LSA.
     * @param rec      (header, body) pair for the LSA.
     * @return (prefix, optional path) — nullopt path means the route should be withdrawn.
     */
    template<typename Policy>
    std::pair<types::IPPrefix, std::optional<OspfPath>> deriveExternalRoute(OspfProcess& process, const LsaKey& key, const std::pair<LsaHeader, LsaBody>& rec);

    /**
     * @brief Recomputes all external routes for the process from the current LSDB state.
     *
     * Iterates every Type-5 LSA in the process-wide LSDB and calls
     * `deriveExternalRoute` for each, returning the full set of valid paths.
     *
     * @tparam Policy  Version policy providing external LSA body accessors.
     * @param process  OSPF process owning the Type-5 LSDB.
     * @return Complete set of (prefix, path) pairs for all reachable externals.
     */
    template<typename Policy>
    std::vector<std::pair<types::IPPrefix, OspfPath>> deriveExternalRoutes(OspfProcess& process);

} // namespace routemanager

} // namespace routing

#endif // OSPF_ROUTE_MANAGER_H

