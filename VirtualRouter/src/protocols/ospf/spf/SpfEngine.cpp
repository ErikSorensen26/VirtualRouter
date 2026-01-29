// SpfEngine.cpp

#include "SpfEngine.h"
#include <OspfArea.h>
#include <OspfProcess.h>
#include <queue>

#include <OspfRegistry.hpp>

namespace OSPF
{
// Min-heap by dist (lazy decrease-key)
struct QItem
{
    uint64_t dist;
    Vertex v;

    bool operator>(const QItem& o) const
    {
        if (dist != o.dist) return dist > o.dist;
        if (v.type != o.v.type) return v.type > o.v.type;
        return v.id > o.v.id;
    }
};

template <typename Policy>
SpfResult SpfEngine::run(SpfTopology<Policy>& topo)
{
    auto& area = topo.area;
    uint32_t rid = area.process().getRouterId();

    SpfResult res;
    res.root = Vertex{VertexType::ROUTER, static_cast<uint64_t>(rid)};
    res.nodes.emplace(res.root, SptNode{0, false, {}});

    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;

    // Confirm root immediately
    res.nodes[res.root].confirmed = true;
    res.confirmedOrder.push_back(res.root);

    RelaxInfo info(res, pq);

    // Seed canidates from root
    expandAndRelax<Policy>(topo, res.root, info);

    // Main loop
    while (!pq.empty())
    {
        QItem cur = pq.top();
        pq.pop();

        auto it = res.nodes.find(cur.v);
        if (it == res.nodes.end()) continue;

        // stale PQ entry?
        if (cur.dist != it->second.dist) continue;

        // already finalized?
        if (it->second.confirmed) continue;

        // finalize
        it->second.confirmed = true;
        res.confirmedOrder.push_back(cur.v);

        expandAndRelax<Policy>(topo, cur.v, info);
    }

    uint8_t maxPaths = area.process().getConfigs().template get<Config::Ospf::MAXIMUM_PATHS>().load();

    for (auto& kv : res.nodes)
    {
        auto& parents = kv.second.parents;

        std::sort(parents.begin(), parents.end(),
            [](const ParentRef& a, const ParentRef& b)
            { 
                if (a.parent == b.parent) return a.ifid < b.ifid;
                return vertexLess(a.parent, b.parent);
            });

        parents.erase(std::unique(parents.begin(), parents.end(), 
            [](const ParentRef& a, const ParentRef& b)
            {
                return a.parent == b.parent && a.ifid == b.ifid;
            }), parents.end());
        
        if (maxPaths > 0 && parents.size() > maxPaths)
            parents.resize(maxPaths);
    }

    return res;
}

template <typename PQ>
void SpfEngine::relaxEdge(
    const Vertex& from,
    const Vertex& to,
    uint64_t newDist,
    uint32_t edgeIfId,
    RelaxInfo<PQ>& info)
{
    auto [it, inserted] = info.out.nodes.try_emplace(
        to, SptNode{std::numeric_limits<uint64_t>::max(), false, {}});
    
    auto& n = it->second;
    if (n.confirmed) return;

    std::vector<uint32_t> firstHops;
    firstHops.reserve(4);

    if (from == info.out.root)
    {
        firstHops.push_back(edgeIfId);
    }
    else
    {
        auto fit = info.out.nodes.find(from);
        if (fit != info.out.nodes.end())
        {
            for (const auto& p : fit->second.parents)
                firstHops.push_back(p.ifid);
        }
        if (firstHops.empty())
            firstHops.push_back(edgeIfId);
    }

    std::sort(firstHops.begin(), firstHops.end());
    firstHops.erase(std::unique(firstHops.begin(), firstHops.end()), firstHops.end());

    auto addParentIfMissing = [&](uint32_t ifid)
    {
        const bool exists = std::any_of(
            n.parents.begin(), n.parents.end(),
            [&](const ParentRef& pr) { return pr.parent == from && pr.ifid == ifid; });

        if (!exists)
            n.parents.push_back(ParentRef{from, ifid});
    };

    if (newDist < n.dist)
    {
        n.dist = newDist;
        n.parents.clear();

        for (uint32_t ifid : firstHops)
            n.parents.push_back(ParentRef{from, ifid});

        info.pq.push(QItem{newDist, to});
    }
    else if (newDist == n.dist)
    {
        for (uint32_t ifid : firstHops)
            addParentIfMissing(ifid);
    }
}

template <typename Policy, typename PQ>
void SpfEngine::expandAndRelax(SpfTopology<Policy>& topo, const Vertex& v, RelaxInfo<PQ>& info)
{
    auto it = info.out.nodes.find(v);
    if (it == info.out.nodes.end()) return;

    const uint64_t base = it->second.dist;
    if (base == std::numeric_limits<uint64_t>::max()) return;

    if (v.type == VertexType::ROUTER)
    {
        auto& edges = topo.edgeScratch;
        if (!topo.expandRouter(static_cast<uint32_t>(v.id), edges)) return;

        for (const auto& e : edges)
        {
            const Vertex& to = e.to;
            const uint32_t c = e.cost;

            const uint64_t nd = addCost(base, c);
            if (nd == std::numeric_limits<uint64_t>::max()) continue;

            relaxEdge(v, to, nd, e.ifid, info);
        }
        return;
    }

    // Network vertex: cost to attached routers is +0
    std::vector<uint32_t>& attached = topo.attachedScratch;
    if (!topo.expandNetwork(v.id, attached)) return;

    for (uint32_t rid : attached)
    {
        Vertex to{VertexType::ROUTER, static_cast<uint64_t>(rid)};
        relaxEdge(v, to, base, 0, info);
    }
}

using NodeMap = std::unordered_map<Vertex, SptNode, VertexHash>;
using PQ = std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>>;

template SpfResult SpfEngine::run<PolicyV2>(SpfTopology<PolicyV2>&);
template SpfResult SpfEngine::run<PolicyV3>(SpfTopology<PolicyV3>&);

template void SpfEngine::relaxEdge<PQ>(const Vertex&, const Vertex&, uint64_t, uint32_t, RelaxInfo<PQ>&);

template void SpfEngine::expandAndRelax<PolicyV2, PQ>(SpfTopology<PolicyV2>&, const Vertex&, RelaxInfo<PQ>&);
template void SpfEngine::expandAndRelax<PolicyV3, PQ>(SpfTopology<PolicyV3>&, const Vertex&, RelaxInfo<PQ>&);
}
