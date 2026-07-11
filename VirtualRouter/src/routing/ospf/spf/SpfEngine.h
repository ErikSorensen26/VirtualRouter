/**
 * @file SpfEngine.h
 * @brief OSPF SPF (Shortest Path First) computation algorithm.
 */

/**
 * @defgroup OSPF_SPF OSPF SPF
 * @ingroup OSPF
 * @brief Shortest Path First computation engine, manager, topology, and type definitions.
 */

#ifndef SPF_ENGINE_H
#define SPF_ENGINE_H

#include <cstdint>
#include <limits>
#include <optional>

#include "SpfTypes.hpp"
#include "SpfTopology.h"

namespace routing::ospf
{
class Area;
struct SpfResult;

/**
 * @brief Stateful Dijkstra engine that computes the OSPF shortest-path tree.
 * @ingroup OSPF_SPF
 *
 * @c SpfEngine implements RFC 2328 §16.1 (OSPFv2) and RFC 5340 §4.8 (OSPFv3)
 * shortest-path-first computation over the graph presented by @ref SpfTopology.
 * It retains the result and edge index from the previous run so that it can
 * detect topology changes and attempt an incremental repair before falling back
 * to a full Dijkstra recompute.
 *
 * ## Architectural Role
 * @c SpfEngine is owned exclusively by @ref SpfManager. It is stateless with
 * respect to the OSPF area — it reads the topology through @ref SpfTopology and
 * writes results into a new @ref SpfResult that @c SpfManager forwards to the
 * RIB. It does not interact with the scheduler, timers, or the LSDB directly.
 *
 * ## Lifecycle & Ownership
 * Constructed once inside @c SpfManager and reused across every SPF
 * computation for the lifetime of the area. The @c last result and @c lastEdges
 * index are updated atomically at the end of each successful run so that the
 * next invocation can compute a diff.
 *
 * ## Concurrency Model
 * All calls to @c run must be serialised by the caller (@ref SpfManager
 * guarantees this via the area's control scheduler). No internal locking is
 * performed.
 *
 * @see SpfManager
 * @see SpfTopology
 */
class SpfEngine
{
public:
    
    /**
     * @brief Constructs the engine bound to its owning manager.
     *
     * Stores the reference only — no computation happens until @ref run.
     * The manager provides configuration access (iSPF enable flag) and
     * serialises every call to @ref run on the area's scheduler.
     *
     * @param mgr The SpfManager that owns and drives this engine; must
     *            outlive it.
     */
    SpfEngine(SpfManager& mgr);


    /**
     * @brief Runs SPF over @p topo and returns the new shortest-path tree.
     *
     * Computes a topology delta against the previous run. If all changes are
     * cost decreases or additions, an incremental repair (@c runIspfRepair) is
     * attempted first. If any edge was removed or its cost increased, or if the
     * repair would produce an incorrect result, a full Dijkstra recompute
     * (@c runFull) is performed instead.
     *
     * After the run the internal edge index and @c last result are updated to
     * reflect the new topology.
     *
     * @tparam Policy  Version-specific LSA policy forwarded to @ref SpfTopology.
     * @param  topo    Topology view built from the current LSDB snapshot.
     * @return         The new @ref SpfResult for this area.
     *
     * @note This is the only public entry point. Call it from @ref SpfManager
     * after constructing a fresh @ref SpfTopology for the current LSDB state.
     */
    template <typename Policy>
    SpfResult run(SpfTopology<Policy>& topo);

private:

    std::optional<SpfResult> last;                              ///< Result from the most recent run; empty on the first invocation.
    std::unordered_map<EdgeKey, EdgeVal, EdgeKeyHash> lastEdges; ///< Flat edge snapshot from the previous run, used to compute @ref SpfDelta.

    // FULL SPF

    /**
     * @brief Executes a complete Dijkstra recompute from scratch.
     *
     * Initialises the candidate list with the local router vertex and runs the
     * standard settle-and-expand loop until the candidate list is empty. All
     * parent information and interface IDs are tracked for ECMP.
     *
     * @tparam Policy  LSA policy forwarded to topology expansion.
     * @param  topo    Topology to expand during the Dijkstra loop.
     * @return         Freshly computed @ref SpfResult.
     */
    template <typename Policy>
    SpfResult runFull(SpfTopology<Policy>& topo);

    // INCREMENTAL SPF

    /**
     * @brief Attempts to repair the previous SPT using only the changed edges.
     *
     * Valid only when @p delta contains no removals or cost increases. Vertices
     * whose distance could have improved are re-relaxed from their existing
     * confirmed positions; vertices unaffected by the delta retain their prior
     * SPT state without re-expansion.
     *
     * @tparam Policy  LSA policy forwarded to topology expansion.
     * @param  topo    Current topology snapshot.
     * @param  delta   Topology changes since the last run; must have no
     *                 REMOVE or COST_INCREASE entries.
     * @return         Repaired @ref SpfResult.
     *
     * @see SpfDelta
     */
    template <typename Policy>
    SpfResult runIspfRepair(SpfTopology<Policy>& topo, const SpfDelta& delta);

    /**
     * @brief Computes the topology delta and refreshes the internal edge index.
     *
     * Walks @p topo to enumerate all current edges, compares them against
     * @c lastEdges, and produces a @ref SpfDelta describing additions, removals,
     * and cost changes. @c lastEdges is then replaced with the new snapshot.
     *
     * @tparam Policy  LSA policy used to enumerate edges from @p topo.
     * @param  topo    Current topology snapshot.
     * @return         Delta describing all changes since the last run.
     */
    template <typename Policy>
    SpfDelta computeDeltaAndUpdateEdgeIndex(SpfTopology<Policy>& topo);

    /**
     * @brief Saturating integer addition for SPF distance accumulation.
     *
     * Returns @c UINT64_MAX if either operand is @c UINT64_MAX (unreachable)
     * or if the result would overflow, preventing wrap-around in pathological
     * topologies.
     *
     * @param base  Current accumulated distance.
     * @param cost  Cost of the next hop to add.
     * @return      @c base + @c cost, clamped to @c UINT64_MAX on overflow.
     */
    static inline uint64_t addCost(uint64_t base, uint32_t cost)
    {
        if (base == std::numeric_limits<uint64_t>::max()) return base;
        if (base > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(cost))
            return std::numeric_limits<uint64_t>::max();
        return base + static_cast<uint64_t>(cost);
    }

    // RELAXATION HELPERS

    /**
     * @brief Relaxes a single edge during a full SPF run.
     *
     * If @p newDist improves the current best distance to @p to, the node's
     * distance is updated, its parent set is rebuilt with the new path, and the
     * priority queue entry is updated. If @p newDist equals the current best
     * distance, the new parent is appended to support ECMP.
     *
     * @tparam PQ            Priority queue type from @ref RelaxInfo.
     * @param  from          Source vertex of the edge being relaxed.
     * @param  to            Destination vertex.
     * @param  newDist       Candidate distance to @p to via @p from.
     * @param  lastHopIfid   Interface ID at the @p from side of the edge.
     * @param  edgeCost      Cost of the edge from @p from to @p to.
     * @param  info          Shared output buffer and priority queue.
     */
    template <typename PQ>
    void relaxEdgeFull(const Vertex& from, const Vertex& to, uint64_t newDist, uint32_t lastHopIfid, uint32_t edgeCost, RelaxInfo<PQ>& info);

    /**
     * @brief Relaxes a single edge during the iSPF repair pass.
     *
     * Behaves like @c relaxEdgeFull but restricts settlement to vertices that
     * were already confirmed in the previous SPT, preventing the repair from
     * settling vertices whose prior paths were invalidated by a cost increase or
     * removal (those require a full recompute).
     *
     * @tparam PQ            Priority queue type from @ref RelaxInfo.
     * @param  from          Source vertex.
     * @param  to            Destination vertex.
     * @param  newDist       Candidate distance via @p from.
     * @param  lastHopIfid   Interface ID at the @p from end.
     * @param  edgeCost      Edge cost.
     * @param  info          Shared output buffer and priority queue.
     */
    template <typename PQ>
    void relaxEdgeRepair(const Vertex& from, const Vertex& to, uint64_t newDist, uint32_t lastHopIfid, uint32_t edgeCost, RelaxInfo<PQ>& info);

    /**
     * @brief Expands vertex @p v and relaxes all of its outgoing edges.
     *
     * Calls @ref SpfTopology::expandRouter or @ref SpfTopology::expandNetwork
     * depending on @p v's type, then invokes @c relaxEdgeFull or
     * @c relaxEdgeRepair (via the mode flag in @p info) for each returned edge.
     *
     * @tparam Policy  LSA policy forwarded to topology expansion.
     * @tparam PQ      Priority queue type.
     * @param  topo    Topology used to enumerate neighbours of @p v.
     * @param  v       Vertex being expanded (just settled from the candidate list).
     * @param  info    Shared relaxation state.
     */
    template <typename Policy, typename PQ>
    void expandAndRelax(SpfTopology<Policy>& topo, const Vertex& v, RelaxInfo<PQ>& info);

    /**
     * @brief Propagates first-hop interface IDs through the shortest-path tree.
     *
     * After Dijkstra settles all vertices, each node's @c parents entries carry
     * the @c lastHopIfid of the immediate link but not yet the outgoing interface
     * at the SPF root. This pass walks the @c confirmedOrder in reverse, copying
     * the root-side @c firstHopIfid from each parent node down to its children.
     *
     * @tparam Policy  LSA policy (unused directly; required for template consistency).
     * @param  res     SPF result to finalise in-place.
     */
    template <typename Policy>
    void finalizeParents(SpfResult& res);

    /**
     * @brief Marks previously-settled vertices as unconfirmed when the iSPF repair cannot guarantee their distances.
     *
     * Any vertex reachable through an edge that was removed or whose cost
     * increased is invalidated so that it will be re-settled by the subsequent
     * repair pass or full recompute.
     *
     * @param res    Previous SPF result; affected nodes are modified in-place.
     * @param delta  Change set identifying removed or cost-increased edges.
     */
    void invalidateForIncreasesAndRemovals(SpfResult& res, const SpfDelta& delta);

    SpfManager& manager; ///< SPF manager holding area information.
};

} // namespace routing::ospf

#endif // SPF_ENGINE_H
