// SpfTopology.h

#ifndef SPF_TOPOLOGY_H
#define SPF_TOPOLOGY_H

#include <unordered_set>
#include "SpfTypes.hpp"

namespace OSPF
{
class OspfArea;
struct LsaRecord;

template <typename RouterLsa, typename NetworkLsa>
class SpfTopology
{
public:
    // Backlink cache
    mutable std::unordered_set<BacklinkKey, BacklinkKeyHash> backlinkCache;

    // Scratch buffers
    mutable std::vector<SpfEdge> edgeScratch;
    mutable std::vector<uint32_t> attachedScratch;

    std::unordered_map<uint64_t, const RouterLsa*> rtr;
    std::unordered_map<uint64_t, const NetworkLsa*> net;

    struct NetV2ByLsId { uint32_t advRouter; const LsaRecord* rec; const NetworkLsa* lsa; };
    std::unordered_map<uint32_t, NetV2ByLsId> netV2ByLsId;

    SpfTopology(const OspfArea& area);
    bool expandRouter(uint32_t rid, std::vector<SpfEdge>& outEdges);
    bool expandNetwork(uint64_t vertexId, std::vector<uint32_t>& attachedRouters);

    const OspfArea& area;
    const bool isV3;
};
}

#endif // SPF_TOPOLOGY_H

