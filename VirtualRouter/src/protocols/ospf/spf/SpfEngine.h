// SpfEngine.h

#ifndef SPF_ENGINE_H
#define SPF_ENGINE_H

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <LSDB.hpp>
#include "SpfTypes.hpp"

namespace OSPF
{
class OspfArea;
struct SpfResult;

class SpfEngine
{
public:
    template <typename RouterLsa, typename NetworkLsa>
    SpfResult run(const OspfArea& area) const;
private:

    static inline uint64_t addCost(uint64_t base, uint32_t cost)
    {
        if (base == std::numeric_limits<uint64_t>::max()) return base;
        if (base > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(cost))
            return std::numeric_limits<uint64_t>::max();
        return base + static_cast<uint64_t>(cost);
    }

    template <typename RouterLsa, typename NetworkLsa>
    struct SpfTopology
    {
        // Backlink cache
        mutable std::unordered_set<BacklinkKey, BacklinkKeyHash> backlinkCache;

        // Scratch buffers
        mutable std::vector<std::pair<Vertex, uint32_t>> edgeScratch;
        mutable std::vector<uint32_t> attachedScratch;

        std::unordered_map<uint64_t, const RouterLsa*> rtr;
        std::unordered_map<uint64_t, const NetworkLsa*> net;

        struct NetV2ByLsId { uint32_t advRouter; const LsaRecord* rec; const NetworkLsa* lsa; };
        std::unordered_map<uint32_t, NetV2ByLsId> netV2ByLsId;

        SpfTopology(const OspfArea& area);
        bool expandRouter(uint32_t rid, std::vector<std::pair<Vertex, uint32_t>>& outEdges);
        bool expandNetwork(uint64_t vertexId, std::vector<uint32_t>& attachedRouters);

        const OspfArea& area;
        const bool isV3;
    };

    template <typename NodeMap, typename PQ>
    static void relaxEdge(const Vertex& from, const Vertex& to, uint64_t newDist, NodeMap& nodes, PQ& pq);

    template <typename RouterLsa, typename NetworkLsa, typename NodeMap, typename PQ>
    static void expandAndRelax(SpfTopology<RouterLsa, NetworkLsa>& topo, const Vertex& v, NodeMap& nodes, PQ& pq);
};
}

#endif // SPF_ENGINE_H
