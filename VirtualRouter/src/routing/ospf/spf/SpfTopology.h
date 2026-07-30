/**
 * @file SpfTopology.h
 * @brief OSPF SPF topology: vertices, edges, tree building.
 */

#ifndef SPF_TOPOLOGY_H
#define SPF_TOPOLOGY_H

#include <unordered_set>
#include "SpfTypes.hpp"

namespace routing::ospf
{
class LsdbTable;
class SpfManager;
class Area;
struct LsaRecord;

/**
 * @brief Policy-parameterised view of the OSPF LSDB as an SPF graph.
 * @ingroup OSPF_SPF
 *
 * @c SpfTopology translates the raw LSDB stored in an @ref Area into the
 * vertex-and-edge model required by @ref SpfEngine. It is constructed once
 * per SPF run and discarded afterward; the @c Area retains authoritative
 * ownership of all LSA memory throughout.
 *
 * The class is split from @ref SpfEngine so that the graph-building logic can
 * be policy-parameterised (OSPFv2 vs OSPFv3) without forcing the entire SPF
 * algorithm to be re-instantiated for each policy. @ref SpfEngine contains only
 * policy-neutral Dijkstra logic and delegates topology expansion to this class.
 *
 * Each instance contains:
 * - A router-LSA index mapping Router-ID to all Router-LSAs originated by that router.
 * - A network-LSA index mapping packed vertex IDs to their Network-LSAs.
 * - An OSPFv2 LS-ID lookup table used to resolve back-links by LS-ID alone.
 * - A backlink cache and scratch buffers shared with the engine during relaxation.
 *
 * ## Architectural Role
 * @c SpfTopology sits between the LSDB and the Dijkstra engine. It presents
 * adjacency lists on demand via @c expandRouter and @c expandNetwork rather
 * than materialising the full graph up front, keeping memory proportional to
 * the fanout of each expanded vertex rather than the square of the topology size.
 *
 * ## Lifecycle & Ownership
 * Constructed by @ref SpfEngine::run (or @ref SpfEngine::runFull) immediately
 * before the Dijkstra loop begins. It borrows a const reference to the @c Area
 * and must not outlive it. The scratch buffers are reused across successive
 * @c expandRouter / @c expandNetwork calls within a single SPF run.
 *
 * ## Concurrency Model
 * @c SpfTopology is single-threaded. All SPF computation runs on the area's
 * control scheduler, serialised through @ref SpfManager. The @c mutable
 * qualifiers on @c backlinkCache and the scratch vectors allow @c const
 * @c Area references to feed the engine without const-casting the topology.
 *
 * @tparam Policy  Version-specific LSA policy. Must provide:
 *                   - @c Policy::RouterLsa  — type of a decoded Router-LSA body.
 *                   - @c Policy::NetworkLsa — type of a decoded Network-LSA body.
 *                   - Static methods for iterating Router-LSA links and
 *                     Network-LSA attached routers in the format expected by
 *                     @c expandRouter and @c expandNetwork.
 *
 * @see SpfEngine
 * @see Area
 */
template <typename Policy>
class SpfTopology
{
public:
    // BACKLINK VALIDATION

    mutable std::unordered_set<BacklinkKey, BacklinkKeyHash> backlinkCache; ///< Confirmed bidirectional link pairs; populated during expansion to avoid re-scanning the LSDB.

    // SCRATCH BUFFERS

    mutable std::vector<SpfEdge>    edgeScratch;      ///< Reusable output buffer for @c expandRouter; avoids per-call allocation.
    mutable std::vector<uint32_t>   attachedScratch;  ///< Reusable output buffer for @c expandNetwork.

    // LSDB INDICES

    std::unordered_map<uint64_t, std::vector<const typename Policy::RouterLsa*>> rtr; ///< Router-ID → all Router-LSAs for that router (multiple LSAs possible in graceful-restart scenarios).
    std::unordered_map<uint64_t, const typename Policy::NetworkLsa*> net;             ///< Packed network vertex ID → Network-LSA.

    /**
     * @brief OSPFv2 supplementary index: LS-ID → (advRouter, LsaRecord*, NetworkLsa*).
     *
     * Used to resolve back-links where only the LS-ID is encoded in the
     * Router-LSA link data.
     */
    struct NetV2ByLsId { uint32_t advRouter; const LsaRecord* rec; const typename Policy::NetworkLsa* lsa; };
    std::unordered_map<uint32_t, NetV2ByLsId> netV2ByLsId;

    /**
     * @brief Constructs the topology index from the LSDB of @p area.
     *
     * Walks all Router-LSAs and Network-LSAs in @p area's LSDB and populates
     * the @c rtr, @c net, and @c netV2ByLsId indices. The area must remain
     * valid and unmodified for the lifetime of this object.
     *
     * @param lsdb  OSPF LSDB.
     * @param area  OSPF area whose LSDB is indexed.
     */
    SpfTopology(SpfManager& mgr);

    /**
     * @brief Expands a router vertex into its outgoing edges.
     *
     * Looks up all Router-LSAs for @p rid, validates back-links, and appends
     * one @ref SpfEdge per usable adjacency to @p outEdges. Links that fail
     * back-link validation (RFC 2328 §16.1 step 2) are silently dropped unless
     * @ref OSPF_STRICT_MISSING_NETWORK_LSA is set.
     *
     * @param rid       Router-ID of the vertex being expanded.
     * @param outEdges  Destination vector; edges are appended (not replaced).
     * @return          True if at least one Router-LSA was found for @p rid.
     *
     * @note The function writes into @c edgeScratch internally before appending
     * to @p outEdges; callers should not hold iterators into @p outEdges across
     * multiple calls.
     */
    bool expandRouter(uint32_t rid, std::vector<SpfEdge>& outEdges);

    /**
     * @brief Expands a transit-network vertex into its set of attached routers.
     *
     * Looks up the Network-LSA identified by @p vertexId and appends the
     * Router-ID of each attached router to @p attachedRouters.
     *
     * @param vertexId         Packed network vertex ID produced by @ref packNetwork.
     * @param attachedRouters  Destination vector; Router-IDs are appended.
     * @return                 True if the Network-LSA was found for @p vertexId.
     */
    bool expandNetwork(uint64_t vertexId, std::vector<uint32_t>& attachedRouters);

    const SpfManager& manager; ///< Spf manager holding area information.
};

} // namespace routing::ospf

#endif // SPF_TOPOLOGY_H
