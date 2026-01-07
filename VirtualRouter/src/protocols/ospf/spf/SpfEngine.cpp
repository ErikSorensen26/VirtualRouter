/ SpfEngine.cpp

#include "SpfEngine.h"
#include <OspfArea.h>
#include <OspfTopology.h>
#include <OspfProcess.h>
#include <queue>

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

template <typename RouterLsa, typename NetworkLsa>
SpfResult SpfEngine::run(const OspfArea& area) const
{
    SpfTopology<RouterLsa, NetworkLsa> topo(area);
    uint32_t rid = area.topology().getProcess().getRouterId();

    SpfResult res;
    res.root = Vertex{VertexType::ROUTER, static_cast<uint64_t>(rid)};
    res.nodes.emplace(res.root, SptNode{0, false, {}});

    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;

    // Confirm root immediately
    res.nodes[res.root].confirmed = true;
    res.confirmedOrder.push_back(res.root);

    // Seed canidates from root
    expandAndRelax<RouterLsa, NetworkLsa>(topo, res.root, res.nodes, pq);

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

        expandAndRelax<RouterLsa, NetworkLsa>(topo, cur.v, res.nodes, pq);
    }

    if (area.getConfigs().deterministicParentOrder.load(std::memory_order_relaxed))
    {
        uint8_t maxPaths = area.topology().getConfigs().maxPaths.load(std::memory_order_relaxed);

        for (auto& kv : res.nodes)
        {
            auto& parents = kv.second.parents;

            std::sort(parents.begin(), parents.end(),
                [](const ParentRef& a, const ParentRef& b) { return vertexLess(a.parent, b.parent); });

            parents.erase(std::unique(parents.begin(), parents.end(), 
                [](const ParentRef& a, const ParentRef& b) { return a.parent == b.parent; }), parents.end());
            
            if (maxPaths > 0 && parents.size() > maxPaths)
                parents.resize(maxPaths);
        }
    }

    return res;
}

template <typename RouterLsa, typename NetworkLsa>
SpfEngine::SpfTopology<RouterLsa, NetworkLsa>::SpfTopology(const OspfArea& area)
    : area(area), isV3(area.topology().getProcess().isV3)
{
    area.lsdb().forEach([this](const LsaKey& key, const LsaRecord* record) {
        if (!record) return;
        const auto& rec = *record;
        const LsaBody& body = rec.body;

        // Router LSAs
        if (std::holds_alternative<RouterLsa>(body))
        {
            auto& lsa = std::get<RouterLsa>(body);
            if (rec.header.age >= OSPF_MAX_AGE) return;
            rtr[key.advertisingRouter] = &lsa;
            return;
        }

        // Network LSAs
        if (std::holds_alternative<NetworkLsa>(body))
        {
            if (rec.header.age >= OSPF_MAX_AGE) return;
            const auto* p = &std::get<NetworkLsa>(body);

            if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV2>)
            {
                auto it2 = netV2ByLsId.find(key.linkStateId);
                auto better = [&](const LsaRecord* a, const LsaRecord* b) -> bool
                {
                    if (a->header.sequence != b->header.sequence)
                        return a->header.sequence > b->header.sequence;
                    if (a->header.checksum != b->header.checksum)
                        return a->header.checksum > b->header.checksum;
                    return a->header.age < b->header.age;
                };

                if (it2 == netV2ByLsId.end() || better(&rec, it2->second.rec))
                {
                    netV2ByLsId[key.linkStateId] = NetV2ByLsId{key.advertisingRouter, &rec, p};
                    const uint64_t nid = packNetwork(key.advertisingRouter, key.linkStateId);
                    net[nid] = p;
                }
            }
            else
            {
                const uint64_t nid = packNetwork(key.advertisingRouter, key.linkStateId);
                net[nid] = p;
            }
            return;
        }
    });
}

template <typename RouterLsa, typename NetworkLsa>
bool SpfEngine::SpfTopology<RouterLsa, NetworkLsa>::expandRouter(uint32_t rid, std::vector<std::pair<Vertex, uint32_t>>& outEdges)
{
    outEdges.clear();

    auto it = rtr.find(rid);
    if (it == rtr.end() || it->second == nullptr) return false;
    const RouterLsa& rlsa = *it->second;

    for (const auto& l : rlsa.links)
    {
        const uint8_t t = l.type;

        if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV2>)
        {
            if (t == static_cast<uint8_t>(OSPFV2_LINK_P2P))
            {
                const uint32_t nbr = l.linkId;

                auto nit = rtr.find(nbr);
                if (nit == rtr.end()) continue;

                // backlink check
                const auto bk = BacklinkKey(VertexType::ROUTER, static_cast<uint64_t>(rid), VertexType::ROUTER, static_cast<uint64_t>(nbr));
                bool backlink = (backlinkCache.find(bk) != backlinkCache.end());
                if (!backlink)
                {
                    for (const auto& bl : nit->second->links)
                    {
                        if (bl.type == OSPFV2_LINK_P2P && bl.linkId == rid)
                        {
                            backlink = true;
                            backlinkCache.insert(bk);
                            break;
                        }
                    }
                }
                if (!backlink) continue;

                Vertex to{VertexType::ROUTER, static_cast<uint64_t>(nbr)};
                outEdges.push_back({to, static_cast<uint32_t>(l.metric)});
            }
            else if (t == OSPFV2_LINK_TRANSIT)
            {
                // linkId is typically Network-LSA LSID; map it to a specific (adv,lsid) if Possible
                const uint32_t netLsId = l.linkId;
                auto nit = netV2ByLsId.find(netLsId);
                if (nit == netV2ByLsId.end()) continue;

                const auto* netlsa = nit->second.lsa;
                const uint64_t netVertexId = packNetwork(nit->second.advRouter, netLsId);

                // Backlink cache
                const auto bk = BacklinkKey(VertexType::ROUTER, static_cast<uint64_t>(rid), VertexType::NETWORK, netVertexId);
                bool backlink = (backlinkCache.find(bk) != backlinkCache.end());
                if (!backlink)
                {
                    if (std::find(netlsa->attachedRouters.begin(),
                                  netlsa->attachedRouters.end(),
                                  rid) != netlsa->attachedRouters.end())
                    {
                        backlink = true;
                        backlinkCache.insert(bk);
                    }
                }
                if (!backlink) continue;

                Vertex to{VertexType::NETWORK, netVertexId};
                outEdges.push_back({to, static_cast<uint32_t>(l.metric)});
            }
            else
            {
                // Stub or unknown, not included in SPF
                continue;
            }
        }
        else
        {
            if (t == static_cast<uint8_t>(OSPFV3_LINK_P2P))
            {
                uint32_t nbr = l.neighborRouterId;

                auto nit = rtr.find(nbr);
                if (nit == rtr.end()) continue;

                // Backlink cache
                const auto bk = BacklinkKey(VertexType::ROUTER, static_cast<uint64_t>(rid), VertexType::ROUTER, static_cast<uint64_t>(nbr));
                bool backlink = (backlinkCache.find(bk) != backlinkCache.end());
                if (!backlink)
                {
                    for (const auto& bl : nit->second->links)
                    {
                        if (bl.type == OSPFV3_LINK_P2P && bl.neighborRouterId == rid)
                        {
                            backlink = true;
                            backlinkCache.insert(bk);
                            break;
                        }
                    }
                }
                if (!backlink) continue;

                Vertex to{VertexType::ROUTER, static_cast<uint64_t>(l.neighborRouterId)};
                outEdges.push_back({to, static_cast<uint32_t>(l.metric)});
            }
            else if (t == OSPFV3_LINK_TRANSIT)
            {
                uint64_t netId = packNetwork(l.neighborRouterId, l.neighborInterfaceId);

                auto nit = net.find(netId);
                if (nit == net.end()) continue;

                const NetworkLsa* netlsa = nit->second;

                const auto bk = BacklinkKey(VertexType::ROUTER, static_cast<uint64_t>(rid), VertexType::NETWORK, netId);
                bool backlink = (backlinkCache.find(bk) != backlinkCache.end());
                if (!backlink)
                {
                    if (std::find(netlsa->attachedRouters.begin(),
                                  netlsa->attachedRouters.end(),
                                  rid) != netlsa->attachedRouters.end())
                    {
                        backlink = true;
                        backlinkCache.insert(bk);
                    }
                }
                if (!backlink) continue;

                Vertex to{VertexType::NETWORK, netId};
                outEdges.push_back({to, static_cast<uint32_t>(l.metric)});
            }
            else
            {
                // Stub or unknown, not included in SPF
                continue;
            }
        }
    }
    return true;
}

template <typename RouterLsa, typename NetworkLsa>
bool SpfEngine::SpfTopology<RouterLsa, NetworkLsa>::expandNetwork(uint64_t vertexId, std::vector<uint32_t>& attachedRouters)
{
    auto it = net.find(vertexId);
    if (it == net.end() || it->second == nullptr) return false;

    attachedRouters.clear();

    const auto* netlsa = it->second;

    for (uint32_t rid : netlsa->attachedRouters)
    {
        auto rit = rtr.find(rid);
        if (rit == rtr.end()) continue;

        // Backlink cache
        const auto bk = BacklinkKey(VertexType::ROUTER, static_cast<uint64_t>(rid), VertexType::NETWORK, vertexId);
        bool backlink = (backlinkCache.find(bk) != backlinkCache.end());
        if (!backlink)
        {
            if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV2>)
            {
                uint32_t netLsId = networkLsId(vertexId);

                for (const auto& l : rit->second->links)
                {
                    if (l.type == OSPFV2_LINK_TRANSIT && l.linkId == netLsId)
                    {
                        backlink = true;
                        backlinkCache.insert(bk);
                        break;
                    }
                }
            }
            else
            {
                for (const auto& l : rit->second->links)
                {
                    if (l.type == OSPFV3_LINK_TRANSIT && packNetwork(l.neighborRouterId, l.neighborInterfaceId) == vertexId)
                    {
                        backlink = true;
                        backlinkCache.insert(bk);
                        break;
                    }
                }
            }
        }

        if (backlink)
            attachedRouters.push_back(rid);
    }

    return true;
}

template <typename NodeMap, typename PQ>
void SpfEngine::relaxEdge(const Vertex& from, const Vertex& to, uint64_t newDist, NodeMap& nodes, PQ& pq)
{
    auto [it, inserted] = nodes.try_emplace(to, SptNode{std::numeric_limits<uint64_t>::max(), false, {}});
    auto& n = it->second;

    if (n.confirmed) return;

    if (newDist < n.dist)
    {
        n.dist = newDist;
        n.parents.clear();
        n.parents.push_back(ParentRef{from});
        pq.push(QItem{newDist, to});
    }
    else if (newDist == n.dist)
    {
        const bool alreadyPresent =
            std::any_of(n.parents.begin(), n.parents.end(),
                        [&](const ParentRef& p) { return p.parent == from; });
        if (!alreadyPresent)
            n.parents.push_back(ParentRef{from});
    }
}

template <typename RouterLsa, typename NetworkLsa, typename NodeMap, typename PQ>
void SpfEngine::expandAndRelax(SpfTopology<RouterLsa, NetworkLsa>& topo, const Vertex& v, NodeMap& nodes, PQ& pq)
{
    auto it = nodes.find(v);
    if (it == nodes.end()) return;

    const uint64_t base = it->second.dist;
    if (base == std::numeric_limits<uint64_t>::max()) return;

    if (v.type == VertexType::ROUTER)
    {
        auto& edges = topo.edgeScratch;
        if (!topo.expandRouter(static_cast<uint32_t>(v.id), edges)) return;

        for (const auto& e : edges)
        {
            const Vertex& to = e.first;
            const uint32_t c = e.second;
            const uint64_t nd = addCost(base, c);
            if (nd == std::numeric_limits<uint64_t>::max()) continue;
            relaxEdge(v, to, nd, nodes, pq);
        }
        return;
    }

    // Network vertex: cost to attached routers is +0
    std::vector<uint32_t>& attached = topo.attachedScratch;
    if (!topo.expandNetwork(v.id, attached)) return;

    for (uint32_t rid : attached)
    {
        Vertex to{VertexType::ROUTER, static_cast<uint64_t>(rid)};
        relaxEdge(v, to, base, nodes, pq);
    }
}


using NodeMap = std::unordered_map<Vertex, SptNode, VertexHash>;
using PQ = std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>>;

template SpfResult SpfEngine::run<RouterLsaV2, NetworkLsaV2>(const OspfArea&) const;
template SpfResult SpfEngine::run<RouterLsaV3, NetworkLsaV3>(const OspfArea&) const;

template void SpfEngine::relaxEdge<NodeMap, PQ>(const Vertex&, const Vertex&, uint64_t, NodeMap&, PQ&);

template void SpfEngine::expandAndRelax<RouterLsaV2, NetworkLsaV2, NodeMap, PQ>(SpfTopology<RouterLsaV2, NetworkLsaV2>&, const Vertex&, NodeMap&, PQ&);
template void SpfEngine::expandAndRelax<RouterLsaV3, NetworkLsaV3, NodeMap, PQ>(SpfTopology<RouterLsaV3, NetworkLsaV3>&, const Vertex&, NodeMap&, PQ&);

template struct SpfEngine::SpfTopology<RouterLsaV2, NetworkLsaV2>;
template struct SpfEngine::SpfTopology<RouterLsaV3, NetworkLsaV3>;
}
