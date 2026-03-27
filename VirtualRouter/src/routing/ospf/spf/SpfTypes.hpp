/**
 * @file SpfTypes.hpp
 * @brief OSPF SPF data structures: vertex, edge, route types.
 */

#ifndef SPF_VERTEX_HPP
#define SPF_VERTEX_HPP

#include <cstdint>
#include <limits>
#include <functional>

#include "ospf/database/LsaKey.hpp"

/// When true, ECMP parent sets are sorted before being stored so that the
/// shortest-path tree is deterministic across recomputes. Disable only if
/// profiling shows the sort cost is unacceptable on very large topologies.
#define OSPF_DETERMINISTIC_PARENT_ORDER true

/// When true, a Network-LSA missing from the LSDB for an apparent transit
/// network is treated as a hard error and the link is dropped. When false
/// (the default), the link is silently skipped, matching most implementations.
#define OSPF_STRICT_MISSING_NETWORK_LSA false

namespace routing::ospf
{

/**
 * @brief Discriminates between router and transit-network vertices in the SPF graph.
 * @ingroup OSPF_SPF
 *
 * RFC 2328 §16.1 models the OSPF topology as a directed graph whose nodes are
 * either routers (identified by Router-ID) or transit networks (identified by
 * the DR's Router-ID and the LS-ID of the Network-LSA).
 */
enum class VertexType : uint8_t
{
    ROUTER  = 1, ///< Router vertex; @c id is the Router-ID.
    NETWORK = 2  ///< Transit-network vertex; @c id is a packed (advRouter, lsId) pair.
};

/**
 * @brief A node in the OSPF SPF directed graph.
 * @ingroup OSPF_SPF
 *
 * A @c Vertex uniquely identifies either a router or a transit network inside
 * one OSPF area. The @c id field is interpreted differently depending on
 * @c type: for routers it is the 32-bit Router-ID zero-extended to 64 bits;
 * for networks it is the value returned by @ref packNetwork.
 *
 * @see packNetwork
 * @see VertexType
 */
struct Vertex
{
    VertexType type{}; ///< Whether this node is a router or a transit network.
    uint64_t   id;     ///< Router-ID (router vertex) or packed (advRouter, lsId) (network vertex).

    friend bool operator==(const Vertex& a, const Vertex& b)
    {
        return a.type == b.type && a.id == b.id;
    }
};

/**
 * @brief Hash functor for @ref Vertex, suitable for use in @c std::unordered_map.
 * @ingroup OSPF_SPF
 */
struct VertexHash
{
    size_t operator()(const ospf::Vertex& v) const noexcept
    {
        return (static_cast<size_t>(v.type) << 1) ^ (static_cast<size_t>(v.id) * 0x9e3779b97f4a7c15ull);
    }
};

/**
 * @brief Identifies a directed edge in the SPF graph, including the outgoing interface.
 * @ingroup OSPF_SPF
 *
 * An edge in the OSPF topology graph connects two vertices. Because a pair of
 * routers may be connected by more than one link, the @c lastHopIfid field
 * breaks ties by recording the interface ID at the @c from end of the edge.
 * This makes equal-cost multipath feasible without merging distinct adjacencies
 * into a single edge.
 */
struct EdgeKey
{
    Vertex   from{};          ///< Source vertex.
    Vertex   to{};            ///< Destination vertex.
    uint32_t lastHopIfid{0};  ///< Interface ID on the @c from side; disambiguates parallel links.

    friend bool operator==(const EdgeKey& a, const EdgeKey& b) noexcept
    {
        return a.from == b.from && a.to == b.to && a.lastHopIfid == b.lastHopIfid;
    }
};

/**
 * @brief Hash functor for @ref EdgeKey.
 * @ingroup OSPF_SPF
 */
struct EdgeKeyHash
{
    size_t operator()(const EdgeKey& k) const noexcept
    {
        size_t h = 0;
        h ^= VertexHash{}(k.from) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= VertexHash{}(k.to)   + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.lastHopIfid) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }
};

/**
 * @brief Metric associated with a directed SPF graph edge.
 * @ingroup OSPF_SPF
 */
struct EdgeVal
{
    uint32_t cost{0}; ///< OSPF link cost in the direction from→to.
};

/**
 * @brief Order-independent key for a pair of vertices, used to detect bidirectional links.
 * @ingroup OSPF_SPF
 *
 * RFC 2328 §16.1 step 2 requires that a Router-LSA link is only usable if a
 * back-link exists in the opposite direction. The @c BacklinkKey normalises the
 * vertex pair so that (A, B) and (B, A) produce the same key, allowing the SPF
 * engine to record confirmed back-links in a flat hash set rather than checking
 * both orderings.
 *
 * The constructor enforces canonical ordering: the lexicographically-smaller
 * vertex is always stored as (t1, id1).
 */
struct BacklinkKey
{
    VertexType t1;  ///< Type of the canonically-first vertex.
    uint64_t   id1; ///< ID of the canonically-first vertex.
    VertexType t2;  ///< Type of the canonically-second vertex.
    uint64_t   id2; ///< ID of the canonically-second vertex.

    /**
     * @brief Constructs a canonical (unordered) key for the given vertex pair.
     *
     * The two vertices are sorted so that the key is identical regardless of
     * which vertex is passed as @p aType/@p aId and which as @p bType/@p bId.
     *
     * @param aType  Type of the first vertex.
     * @param aId    ID of the first vertex.
     * @param bType  Type of the second vertex.
     * @param bId    ID of the second vertex.
     */
    BacklinkKey(VertexType aType, uint64_t aId, VertexType bType, uint64_t bId)
    {
        if (aType < bType || (aType == bType && aId <= bId))
        {
            t1 = aType; id1 = aId;
            t2 = bType; id2 = bId;
        }
        else
        {
            t1 = bType; id1 = bId;
            t2 = aType; id2 = aId;
        }
    }

    bool operator==(const BacklinkKey& o) const noexcept
    {
        return t1 == o.t1 && id1 == o.id1 && t2 == o.t2 && id2 == o.id2;
    }
};

/**
 * @brief Hash functor for @ref BacklinkKey.
 * @ingroup OSPF_SPF
 */
struct BacklinkKeyHash
{
    size_t operator()(const BacklinkKey& k) const noexcept
    {
        size_t h = 0;
        h ^= std::hash<uint64_t>{}((uint64_t(k.t1) << 56) | k.id1);
        h ^= std::hash<uint64_t>{}((uint64_t(k.t2) << 56) | k.id2);
        return h;
    }
};

/**
 * @brief Packs an (advRouter, lsId) pair into a single 64-bit network vertex ID.
 *
 * The high 32 bits hold the advertising router's Router-ID; the low 32 bits
 * hold the LS-ID of the Network-LSA. Use @ref networkAdvRouter and
 * @ref networkLsId to unpack.
 *
 * @param advRouter  Router-ID of the DR that originated the Network-LSA.
 * @param lsId       LS-ID field of the Network-LSA.
 * @return           Packed 64-bit vertex ID.
 */
static inline uint64_t packNetwork(uint32_t advRouter, uint32_t lsId)
{
    return (static_cast<uint64_t>(advRouter) << 32) | static_cast<uint64_t>(lsId);
}

/// @brief Extracts the advertising router's Router-ID from a packed network vertex ID.
static inline uint32_t networkAdvRouter(uint64_t packed) { return static_cast<uint32_t>(packed >> 32); }

/// @brief Extracts the LS-ID from a packed network vertex ID.
static inline uint32_t networkLsId(uint64_t packed) { return static_cast<uint32_t>(packed & 0xFFFFFFFFu); }

/**
 * @brief Reconstructs the @ref LsaKey for the Network-LSA that owns a packed network vertex ID.
 *
 * @param packed  Packed network vertex ID produced by @ref packNetwork.
 * @param type    LSA type field (OSPFv2: 2; OSPFv3: 0x2002).
 * @return        Fully-formed @ref LsaKey for the corresponding Network-LSA.
 */
static inline LsaKey networkLsaKey(uint64_t packed, uint16_t type) { return LsaKey{type, networkLsId(packed), networkAdvRouter(packed)};}

/**
 * @brief Imposes a deterministic total order on vertices for use in sorted containers.
 *
 * Router vertices sort before network vertices; within the same type, vertices
 * are ordered by ascending ID. This ordering is used when
 * @ref OSPF_DETERMINISTIC_PARENT_ORDER is enabled to ensure ECMP parent sets
 * are identical across independent SPF runs on the same topology.
 *
 * @param a  First vertex.
 * @param b  Second vertex.
 * @return   True if @p a should precede @p b.
 */
static inline bool vertexLess(const Vertex& a, const Vertex& b)
{
    if (a.type != b.type) return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
    return a.id < b.id;
}

/**
 * @brief Describes the topology changes detected between two consecutive SPF runs.
 * @ingroup OSPF_SPF
 *
 * The incremental SPF (iSPF) path uses this delta to decide whether a full
 * Dijkstra recompute is needed or whether the existing shortest-path tree can
 * be repaired locally. A full recompute is always triggered if any edge was
 * removed or its cost increased; cost decreases and additions can be handled
 * by the cheaper repair pass.
 *
 * @see SpfEngine::runIspfRepair
 */
struct SpfDelta
{
    /**
     * @brief A single topology change for one directed edge.
     * @ingroup OSPF_SPF
     */
    struct Change
    {
        /**
         * @brief Classifies the nature of the topology change.
         */
        enum class Kind : uint8_t
        {
            ADD,           ///< A new edge appeared in the topology.
            REMOVE,        ///< An existing edge was withdrawn.
            COST_DECREASE, ///< The edge cost dropped; existing paths can only improve.
            COST_INCREASE  ///< The edge cost rose; previously optimal paths may now be suboptimal.
        };

        Kind     kind{};       ///< Type of change.
        EdgeKey  key{};        ///< Identifies the affected edge.
        uint32_t oldCost{0};   ///< Cost before the change (0 for ADD).
        uint32_t newCost{0};   ///< Cost after the change (0 for REMOVE).
    };

    bool hasAnyChange{false};             ///< True if @c changes is non-empty.
    bool hasAnyIncreaseOrRemove{false};   ///< True if any change requires a full SPF recompute.
    std::vector<Change> changes;          ///< Per-edge change records.
};

/**
 * @brief Records how a vertex was reached during the SPF computation.
 * @ingroup OSPF_SPF
 *
 * Multiple @c ParentRef entries on the same @ref SptNode indicate equal-cost
 * multipaths. The @c firstHopIfid is propagated outward from the root and
 * represents the local outgoing interface; @c lastHopIfid is the interface ID
 * at the immediate parent, used for back-link validation.
 */
struct ParentRef
{
    Vertex   parent{};         ///< The directly-preceding vertex on this path.
    uint32_t firstHopIfid{0};  ///< Outgoing interface ID at the SPF root for this path.
    uint32_t lastHopIfid{0};   ///< Interface ID on the parent's side of the link.
    uint32_t edgeCost{0};      ///< Cost of the edge from @c parent to this vertex.
};

/**
 * @brief A candidate edge emitted by topology expansion, used as SPF relaxation input.
 * @ingroup OSPF_SPF
 *
 * @c SpfEdge instances are produced by @ref SpfTopology::expandRouter and
 * @ref SpfTopology::expandNetwork. They are short-lived scratch values; do not
 * hold pointers into them across topology expansions.
 */
struct SpfEdge
{
    Vertex   to{};    ///< Destination vertex of this edge.
    uint32_t cost{0}; ///< OSPF metric for traversing this edge.
    uint32_t ifid{0}; ///< Interface ID at the source end; used to populate @ref ParentRef::lastHopIfid.
};

/**
 * @brief Per-vertex state maintained in the candidate list and shortest-path tree.
 * @ingroup OSPF_SPF
 *
 * Each entry in @ref SpfResult::nodes is an @c SptNode. The @c dist field
 * holds the current best known distance from the SPF root. Once @c confirmed
 * is set, the vertex has been moved from the candidate list to the SPT and its
 * @c dist value is final. Multiple @c parents entries indicate ECMP paths.
 *
 * @warning Do not read @c parents until @c confirmed is true; the parent set
 * may be partially updated during the relaxation phase.
 */
struct SptNode
{
    uint64_t dist = std::numeric_limits<uint64_t>::max(); ///< Best known distance; @c UINT64_MAX means unreachable.
    bool confirmed = false;                                ///< True once this vertex is settled in the SPT.
    std::vector<ParentRef> parents;                        ///< ECMP parent set for this vertex.
};

/**
 * @brief The complete output of one SPF computation for a single OSPF area.
 * @ingroup OSPF_SPF
 *
 * @c SpfResult is produced by @ref SpfEngine::run and consumed by
 * @ref SpfManager, which translates it into RIB updates. The @c confirmedOrder
 * vector preserves the settlement sequence so that route installation can
 * proceed in the same order as the SPF algorithm settled vertices, which is
 * important for deterministic convergence.
 *
 * @see SpfEngine
 * @see SpfManager
 */
struct SpfResult
{
    Vertex root;                                                  ///< The local router's vertex (SPF source).
    std::unordered_map<Vertex, SptNode, VertexHash> nodes;        ///< Per-vertex SPT state, keyed by vertex.
    std::vector<Vertex> confirmedOrder;                           ///< Vertices in the order they were settled.
};

/**
 * @brief Bundles the mutable output and priority queue for edge relaxation during SPF.
 * @ingroup OSPF_SPF
 *
 * Passed by reference through the recursive expansion and relaxation helpers so
 * that they share the same @ref SpfResult output buffer and priority queue
 * without passing both as separate parameters everywhere.
 *
 * @tparam PQ  Priority queue type used by the SPF candidate list. Must support
 *             @c push(Vertex, uint64_t) and @c decreaseKey(Vertex, uint64_t).
 */
template <typename PQ>
struct RelaxInfo
{
    /**
     * @brief Constructs a @c RelaxInfo bound to the given output result and priority queue.
     *
     * @param r  SPF result buffer that relaxation writes into.
     * @param q  Priority queue managing the candidate vertex set.
     */
    RelaxInfo(SpfResult& r, PQ& q)
        : out(r), pq(q) {}

    bool repairMode{false}; ///< When true, only vertices already in the SPT may be re-settled (iSPF repair path).

    SpfResult& out; ///< Destination for relaxation updates.
    PQ& pq;         ///< Candidate list; drives the next-vertex selection loop.
};

} // namespace routing::ospf

#endif // SPF_VERTEX_HPP
