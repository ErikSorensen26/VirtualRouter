// SpfEngine.h

#ifndef SPF_ENGINE_H
#define SPF_ENGINE_H

#include <cstdint>
#include <limits>
#include <LSDB.hpp>
#include "SpfTypes.hpp"
#include "SpfTopology.h"

namespace OSPF
{
class OspfArea;
struct SpfResult;

class SpfEngine
{
public:

    template <typename Policy>
    SpfResult run(SpfTopology<Policy>& topo);
private:

    std::optional<SpfResult> last;
    std::unordered_map<EdgeKey, EdgeVal, EdgeKeyHash> lastEdges;

    // Full SPF
    template <typename Policy>
    SpfResult runFull(SpfTopology<Policy>& topo);

    // iSPF repair
    template <typename Policy>
    SpfResult runIspfRepair(SpfTopology<Policy>& topo, const SpfDelta& delta);

    template <typename Policy>
    SpfDelta computeDeltaAndUpdateEdgeIndex(SpfTopology<Policy>& topo);

    static inline uint64_t addCost(uint64_t base, uint32_t cost)
    {
        if (base == std::numeric_limits<uint64_t>::max()) return base;
        if (base > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(cost))
            return std::numeric_limits<uint64_t>::max();
        return base + static_cast<uint64_t>(cost);
    }

    template <typename PQ>
    void relaxEdgeFull(const Vertex& from, const Vertex& to, uint64_t newDist, uint32_t lastHopIfid, uint32_t edgeCost, RelaxInfo<PQ>& info);

    template <typename PQ>
    void relaxEdgeRepair(const Vertex& from, const Vertex& to, uint64_t newDist, uint32_t lastHopIfid, uint32_t edgeCost, RelaxInfo<PQ>& info);

    template <typename Policy, typename PQ>
    void expandAndRelax(SpfTopology<Policy>& topo, const Vertex& v, RelaxInfo<PQ>& info);

    template <typename Policy>
    void finalizeParents(SpfTopology<Policy>& topo, SpfResult& res);

    void invalidateForIncreasesAndRemovals(SpfResult& res, const SpfDelta& delta);
};
}

#endif // SPF_ENGINE_H
