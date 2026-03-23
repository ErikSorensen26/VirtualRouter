// SpfTopology.h

#ifndef SPF_TOPOLOGY_H
#define SPF_TOPOLOGY_H

#include <unordered_set>
#include "SpfTypes.hpp"

namespace routing::ospf
{
class Area;
struct LsaRecord;

template <typename Policy>
class SpfTopology
{
public:
    // Backlink cache
    mutable std::unordered_set<BacklinkKey, BacklinkKeyHash> backlinkCache;

    // Scratch buffers
    mutable std::vector<SpfEdge> edgeScratch;
    mutable std::vector<uint32_t> attachedScratch;

    std::unordered_map<uint64_t, std::vector<const typename Policy::RouterLsa*>> rtr;
    std::unordered_map<uint64_t, const typename Policy::NetworkLsa*> net;

    struct NetV2ByLsId { uint32_t advRouter; const LsaRecord* rec; const typename Policy::NetworkLsa* lsa; };
    std::unordered_map<uint32_t, NetV2ByLsId> netV2ByLsId;

    SpfTopology(const Area& area);
    bool expandRouter(uint32_t rid, std::vector<SpfEdge>& outEdges);
    bool expandNetwork(uint64_t vertexId, std::vector<uint32_t>& attachedRouters);

    const Area& area;
};
} // namespace routing

#endif // SPF_TOPOLOGY_H


