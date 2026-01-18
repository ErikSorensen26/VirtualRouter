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
    static SpfResult run(SpfTopology<Policy>& topo);
private:

    static inline uint64_t addCost(uint64_t base, uint32_t cost)
    {
        if (base == std::numeric_limits<uint64_t>::max()) return base;
        if (base > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(cost))
            return std::numeric_limits<uint64_t>::max();
        return base + static_cast<uint64_t>(cost);
    }

    template <typename PQ>
    struct RelaxInfo
    {
        RelaxInfo(SpfResult& r, PQ& q)
            : out(r), pq(q) {}
        
        SpfResult& out;
        PQ& pq;
    };

    template <typename PQ>
    static void relaxEdge(const Vertex& from, const Vertex& to, uint64_t newDist, uint32_t edgeIfid, RelaxInfo<PQ>& info);

    template <typename Policy, typename PQ>
    static void expandAndRelax(SpfTopology<Policy>& topo, const Vertex& v, RelaxInfo<PQ>& info);
};
}

#endif // SPF_ENGINE_H
