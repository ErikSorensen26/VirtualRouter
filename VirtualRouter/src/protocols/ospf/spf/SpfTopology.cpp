// SpfTopology.cpp

#include "SpfTopology.h"
#include <OspfArea.h>
#include <OspfTopology.h>
#include <OspfProcess.h>

namespace OSPF
{
template <typename Policy>
SpfTopology<Policy>::SpfTopology(const OspfArea& area)
    : area(area)
{
    std::vector<LsaKey> expired;

    area.lsdb().forEach([this](const LsaKey& key, const LsaRecord* record)
    {
        if (!record) return;
        const auto& rec = *record;
        const LsaBody& body = rec.body;

        // Router LSAs
        if (std::holds_alternative<typename Policy::RouterLsa>(body))
        {
            auto& lsa = std::get<typename Policy::RouterLsa>(body);
            if (rec.header.age >= OSPF_MAX_AGE) return;
            if constexpr (std::is_same_v<Policy, PolicyV2>)
                if (key.advertisingRouter != key.linkStateId)
                    return;  // Allow 1 entry for ospfv2
            rtr[key.advertisingRouter].push_back(&lsa);
            return;
        }

        // Network LSAs
        if (std::holds_alternative<typename Policy::NetworkLsa>(body))
        {
            if (rec.header.age >= OSPF_MAX_AGE) return;
            const auto* p = &std::get<typename Policy::NetworkLsa>(body);

            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV2>)
            {
                auto it2 = netV2ByLsId.find(key.linkStateId);
                auto better = [&](const LsaRecord* a, const LsaRecord* b) -> bool
                {
                    if (a->header.sequence != b->header.sequence)
                        return a->header.sequence > b->header.sequence;
                    if (a->header.checksum != b->header.checksum) return a->header.checksum > b->header.checksum;
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

template <typename Policy>
bool SpfTopology<Policy>::expandRouter(uint32_t rid, std::vector<SpfEdge>& outEdges)
{
    outEdges.clear();

    auto it = rtr.find(rid);
    if (it == rtr.end() || it->second.empty()) return false;
    const std::vector<const typename Policy::RouterLsa*> rlsas = it->second;

    for (const auto* rlsa : rlsas)
    {
        auto extractIfId = [](const auto& l) -> uint32_t
        {
            if constexpr (requires { l.ifid; })
                return static_cast<uint32_t>(l.ifid);
            else if constexpr (requires { l.interfaceId; })
                return static_cast<uint32_t>(l.interfaceId);
            else if constexpr (requires { l.linkData; })
                return static_cast<uint32_t>(l.linkData);
            else
                return 0;
        };

        for (const auto& l : rlsa->links)
        {
            const uint8_t t = l.type;
            const uint32_t ifid = extractIfId(l);

            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV2>)
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
                        for (const auto& bl : nit->second.front()->links)
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
                    outEdges.push_back({to, static_cast<uint32_t>(l.metric), ifid});
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
                    outEdges.push_back({to, static_cast<uint32_t>(l.metric), ifid});
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
                        for (const auto& links : nit->second)
                        {
                            for (const auto& bl : links->links)
                            {
                                if (bl.type == OSPFV3_LINK_P2P && bl.neighborRouterId == rid)
                                {
                                    backlink = true;
                                    backlinkCache.insert(bk);
                                    break;
                                }
                            }
                        }
                    }
                    if (!backlink) continue;

                    Vertex to{VertexType::ROUTER, static_cast<uint64_t>(l.neighborRouterId)};
                    outEdges.push_back({to, static_cast<uint32_t>(l.metric), ifid});
                }
                else if (t == OSPFV3_LINK_TRANSIT)
                {
                    uint64_t netId = packNetwork(l.neighborRouterId, l.neighborInterfaceId);

                    auto nit = net.find(netId);
                    if (nit == net.end()) continue;

                    const typename Policy::NetworkLsa* netlsa = nit->second;

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
                    outEdges.push_back({to, static_cast<uint32_t>(l.metric), ifid});
                }
                else
                {
                    // Stub or unknown, not included in SPF
                    continue;
                }
            }
        }
    }
    return true;
}

template <typename Policy>
bool SpfTopology<Policy>::expandNetwork(uint64_t vertexId, std::vector<uint32_t>& attachedRouters)
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
            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV2>)
            {
                uint32_t netLsId = networkLsId(vertexId);

                for (const auto& l : rit->second.front()->links)
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
                for (const auto& links : rit->second)
                {
                    for (const auto& l : links->links)
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
        }

        if (backlink)
            attachedRouters.push_back(rid);
    }

    return true;
}

template class SpfTopology<PolicyV2>;
template class SpfTopology<PolicyV3>;
}
