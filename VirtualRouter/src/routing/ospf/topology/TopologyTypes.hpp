/**
 * @file TopologyTypes.hpp
 * @brief Shared OSPF topology and route data structures used across SPF, RIB, and flooding.
 */

#ifndef OSPF_TOPOLOGY_TYPES_HPP
#define OSPF_TOPOLOGY_TYPES_HPP

#include <IPAddress.h>
#include <optional>

namespace routing::ospf
{
struct Vertex;
struct VertexHash;

/**
 * @brief Classifies an OSPF route by its origin relative to the local area.
 * @ingroup OSPF_TOPOLOGY
 */
enum class OspfRouteType : uint8_t
{
    INTRA_AREA = 0, ///< Route learned entirely within the local area (intra-area SPF result).
    INTER_AREA = 1, ///< Route learned via an ABR summary (Type-3/Type-4 LSA).
    EXTERNAL   = 2, ///< Route redistributed into OSPF from another protocol (Type-5 LSA).
    NSSA       = 3, ///< External route learned through an NSSA area (Type-7 LSA).
};

/**
 * @brief Identifies the outgoing interface and gateway for one OSPF equal-cost path.
 * @ingroup OSPF_TOPOLOGY
 *
 * OSPF supports ECMP; a single route may carry multiple `OspfNextHop` entries,
 * one per equal-cost forwarding path produced by the SPF run.
 */
struct OspfNextHop
{
    uint32_t interfaceId{};   ///< Index of the outgoing interface.
    types::IPAddress nextHop{}; ///< Gateway address, or the unspecified address for directly connected links.

    bool operator==(const OspfNextHop& o) const
    {
        return interfaceId == o.interfaceId &&
               nextHop == o.nextHop;
    }

    bool operator<(const OspfNextHop& o) const
    {
        return std::tie(interfaceId, nextHop)
             < std::tie(o.interfaceId, o.nextHop);
    }
};

/**
 * @brief SPF-derived reachability record for a remote OSPF router (vertex).
 * @ingroup OSPF_TOPOLOGY
 *
 * Produced by the SPF engine for every router vertex that was settled during
 * the Dijkstra run. The `TopologyTable` consumes these to track ASBR reachability
 * for external route computation.
 */
struct OspfRouter
{
    uint32_t rid;                      ///< Router ID of the remote router.
    uint64_t cost;                     ///< Total path cost from the computing router.
    std::vector<OspfNextHop> nextHops; ///< ECMP forwarding entries to reach this router.
};

/**
 * @brief Best-path reachability summary for a router, stored in the topology table.
 * @ingroup OSPF_TOPOLOGY
 *
 * Unlike @ref OspfRouter (which is per-area), `RouterReach` reflects the
 * winning entry after the topology table merges intra-area and inter-area
 * candidates for the same RID.
 */
struct RouterReach
{
    uint64_t cost;                     ///< Winning path cost to the router.
    std::vector<OspfNextHop> nextHops; ///< ECMP forwarding entries for the winning path.
};

/**
 * @brief A single candidate forwarding path for an OSPF prefix.
 * @ingroup OSPF_TOPOLOGY
 *
 * Multiple `OspfPath` objects may exist for the same prefix (one per area or
 * per originating ASBR). `OspfRib` selects the best among all candidates
 * according to RFC 2328 §16 preference rules and stores the winner in an
 * @ref OspfRoute.
 */
struct OspfPath
{
    OspfRouteType type{};              ///< Route type that determines preference order.
    std::optional<uint32_t> area{};    ///< Area this path was learned from; nullopt for process-wide externals.
    uint64_t cost{};                   ///< Path cost (metric).
    uint8_t adminDistance{};           ///< Administrative distance applied when installing into the global RIB.
    uint8_t options{};                 ///< OSPF options bits carried with this path (used for E-bit comparison).
    bool discard{false};               ///< True if this is an area-range discard (suppress-and-drop) path.
    bool suppressed{false};            ///< True if this path is hidden by an area range aggregate.
    std::vector<OspfNextHop> nextHops{};

    friend bool operator==(const OspfPath& a, const OspfPath& b)
    {
        return a.options == b.options && a.area == b.area &&
               a.type == b.type && a.cost == b.cost &&
               a.adminDistance == b.adminDistance &&
               a.nextHops == b.nextHops;
    }
};

/**
 * @brief The installed OSPF route for a prefix, representing the winning candidate path.
 * @ingroup OSPF_TOPOLOGY
 *
 * Stored inside `OspfRib::PrefixState` once a best path has been selected.
 * A route compares equal to another if all attributes that affect installation
 * into the global RIB are the same; next-hop churn alone does not constitute
 * a route change for purposes of summary origination.
 */
struct OspfRoute
{
    types::IPPrefix prefix{};
    uint8_t options{};
    uint64_t cost{};
    uint8_t adminDistance{};
    OspfRouteType type{};
    std::optional<uint32_t> area{};    ///< Source area; nullopt for process-wide external routes.
    bool suppressed{false};            ///< True if this route is suppressed by an intra-area range.
    std::vector<OspfPath> paths{};     ///< All candidate paths; the first entry is the selected path.

    /// Returns true if this route's installed attributes match `other` (path comparison ignores next-hops).
    bool operator==(const OspfPath& other)
    {
        return options == other.options &&
               type == other.type &&
               area == other.area &&
               cost == other.cost &&
               adminDistance == other.adminDistance;
    }

    /// Returns true if all installed attributes, including suppression state, match `other`.
    bool operator==(const OspfRoute& other)
    {
        return type == other.type &&
               cost == other.cost &&
               options == other.options &&
               adminDistance == other.adminDistance &&
               area == other.area &&
               suppressed == other.suppressed;
    }
};

/**
 * @brief Describes a single route add or removal event produced by @ref OspfRib.
 * @ingroup OSPF_TOPOLOGY
 *
 * Returned in batches from `replaceArea` and `replaceRoute` so that callers
 * (e.g., the ABR summary originator) can react to exactly what changed without
 * re-scanning the entire OSPF RIB.
 */
struct OspfRouteChange
{
    types::IPPrefix prefix{};
    uint8_t options{};
    uint64_t cost{};
    bool isRemoval{false}; ///< True if the route was withdrawn; false if it was added or updated.
};

/**
 * @brief Constructs an @ref OspfPath from its constituent attributes.
 * @ingroup OSPF_TOPOLOGY
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
inline OspfPath makePath(
    std::optional<uint32_t> areaId,
    uint8_t options,
    uint8_t adminDistance,
    uint64_t cost,
    std::vector<OspfNextHop> nextHops,
    OspfRouteType type)
{
    return OspfPath{
        .type = type,
        .area = areaId,
        .cost = cost,
        .adminDistance = adminDistance,
        .options = options,
        .nextHops = std::move(nextHops)
    };
}

/// Per-vertex next-hop cache populated during SPF to avoid redundant next-hop resolution.
using NhCache = std::unordered_map<Vertex, std::vector<OspfNextHop>, VertexHash>;
} // namespace routing

#endif // OSPF_TOPOLOGY_TYPES_HPP

