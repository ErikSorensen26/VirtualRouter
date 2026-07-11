/**
 * @file RouteManagerUtility.h
 * @brief Shared internals for the three route managers: private-member access and SPF next-hop resolution.
 */

#ifndef OSPF_ROUTE_MANAGER_UTILITY_H
#define OSPF_ROUTE_MANAGER_UTILITY_H

#include <optional>
#include <vector>

#include "TopologyTypes.hpp"
#include "ospf/spf/SpfTypes.hpp"

namespace config { struct OspfRegistry; }

namespace routing::ospf
{
class Area;
class OspfProcess;
class TopologyTable;
class LsdbTable;
class ExternalOriginator;

/**
 * @brief Access- and next-hop-resolution helpers shared by the three route managers.
 * @ingroup OSPF_TOPOLOGY
 *
 * `RouteManagerUtility` is friended by @ref Area and @ref OspfProcess so the
 * route managers can reach the private state they need (LSDB, topology table,
 * configs, external LSDB) without each becoming a friend of those classes
 * themselves.  Everything here is private and exposed only to
 * @ref IntraRouteManager, @ref InterRouteManager, and
 * @ref ExternalRouteManager — the utility must not become a general backdoor
 * into `Area`/`OspfProcess` internals.
 *
 * All functions are stateless and run on the owning process's single-threaded
 * `ProcessQueue`, like their callers.
 */
struct RouteManagerUtility
{
private:
    friend class IntraRouteManager;
    friend class InterRouteManager;
    friend class ExternalRouteManager;

    // PRIVATE-MEMBER ACCESSORS

    static TopologyTable& getTopoTable(OspfProcess& proc);
    static const config::OspfRegistry& getProcessConfigs(OspfProcess& proc);
    static LsdbTable& getLsdb(Area& area);
    static ExternalOriginator& getExtOriginator(OspfProcess& proc);

    // NEXT-HOP RESOLUTION

    /**
     * @brief Resolves the next hop for a vertex whose SPF parent is the root (directly connected).
     *
     * Looks up the first-hop interface and the neighbor matching the vertex's
     * router ID; returns the neighbor's address as the next hop.
     *
     * @param area Area whose interfaces and neighbor tables are consulted.
     * @param v    Destination vertex (router or network).
     * @param pref Parent reference recording the first-hop interface ID.
     * @return Next hop, or std::nullopt if the interface or neighbor is gone.
     */
    static std::optional<OspfNextHop> resolveDirectNextHop(Area& area, const Vertex& v, const ParentRef& pref);

    /**
     * @brief Sorts and removes duplicate (interface, address) next-hop pairs in place.
     */
    static void dedupeNextHops(std::vector<OspfNextHop>& hops);

    /**
     * @brief Recursively resolves all equal-cost next hops for a vertex from the SPF tree.
     *
     * Walks the vertex's parents up to the root, memoising results in `cache`
     * so shared sub-paths are resolved once per SPF-derivation pass.
     *
     * @param area  Area providing interface and neighbor lookups.
     * @param v     Destination vertex.
     * @param spf   Completed SPF result.
     * @param cache Per-pass memo of already-resolved vertices.
     * @return Deduplicated next hops; empty if the vertex is unreachable.
     */
    static std::vector<OspfNextHop> computeNextHops(Area& area, const Vertex& v, const SpfResult& spf, NhCache& cache);

    /**
     * @brief Resolves reachability of an ABR/ASBR within one area's SPF result.
     *
     * @param area    Area whose SPF result is consulted.
     * @param spf     Completed SPF result.
     * @param abrRid  Router ID of the border router to resolve.
     * @param nhCache Per-pass next-hop memo shared with computeNextHops.
     * @return (cost, next hops) to the router, or std::nullopt if unreachable.
     */
    static std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveToAbr(Area& area, const SpfResult& spf, uint32_t abrRid, NhCache& nhCache);

    /**
     * @brief Resolves reachability of a border router via the process-wide topology table.
     *
     * @param proc   Process whose topology table is consulted.
     * @param abrRid Router ID of the border router to resolve.
     * @return The stored reachability entry, or std::nullopt if unknown.
     */
    static std::optional<RouterReach> resolveToAbrs(OspfProcess& proc, uint32_t abrRid);
};
}

#endif // OSPF_ROUTE_MANAGER_UTILITY_H
