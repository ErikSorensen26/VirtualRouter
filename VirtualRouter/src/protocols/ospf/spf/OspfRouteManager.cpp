// OspfRouteManager.cpp

#include "OspfRouteManager.h"
#include <OspfProcess.h>
#include <OspfTopology.h>
#include <OspfArea.h>
#include <OspfInterface.h>
#include <OspfNeighborTable.h>
#include <OspfNeighbor.h>
#include <VirtualRouter.h>
#include <LsdbTable.h>
#include <assert.h>

#include "OspfRoutingTable.h"

#include <IntraAreaPrefixLsa.hpp>

namespace OSPF
{
std::optional<OspfNextHop> resolveDirectNextHop(OspfArea& area, const Vertex& v, const ParentRef& pref)
{
    uint32_t rid = v.type == VertexType::ROUTER
        ? static_cast<uint32_t>(v.id)
        : networkAdvRouter(v.id);

    auto& ifaceMgr = area.topology().process.getIfaceMgr();

    OspfInterface* iface = ifaceMgr.getInterface({pref.ifid, area.areaId});
    if (!iface) return std::nullopt;

    Neighbor* nbr = iface->getNTable().lookup(rid);
    if (!nbr) return std::nullopt;

    return OspfNextHop{pref.ifid, nbr->ipAddress};
}

void dedupe(std::vector<OspfNextHop>& hops)
{
    auto eq = [](const OspfNextHop& a, const OspfNextHop& b)
    {
        return a.interfaceId == b.interfaceId &&
               a.nextHop == b.nextHop;
    };

    std::vector<OspfNextHop> unique;
    unique.reserve(hops.size());

    for (const auto& nh : hops)
    {
        if (std::find_if(unique.begin(), unique.end(),
            [&](const OspfNextHop& u) { return eq(u, nh); }) == unique.end())
        {
            unique.push_back(nh);
        }
    }

    hops.swap(unique);
}

std::vector<OspfNextHop> computeNextHops(OspfArea& area, const Vertex& v, const SpfResult& spf, RouteManager::NhCache& cache)
{
    if (auto it = cache.find(v); it != cache.end())
        return it->second;

    std::vector<OspfNextHop> result;
    const auto& node = spf.nodes.at(v);

    for (const auto& pref : node.parents)
    {
        const Vertex& p = pref.parent;

        if (p == spf.root)
        {
            if (auto nh = resolveDirectNextHop(area, v, pref))
                result.push_back(*nh);
        }
        else
        {
            auto parentNhs = computeNextHops(area, p, spf, cache);
            result.insert(result.end(), parentNhs.begin(), parentNhs.end());
        }
    }

    dedupe(result);
    cache[v] = result;
    return result;
}

template <typename RouterLsa, typename NetworkLsa>
std::vector<std::pair<IPPrefix, OspfPath>> RouteManager::deriveIntraAreaRouters(const SpfResult& spf, OspfArea& area)
{
    auto& lsdb = area.lsdb();
    uint8_t adminDistance = area.topology().getConfigs().distance.load(std::memory_order_relaxed);

    NhCache nhCache;
    std::vector<std::pair<IPPrefix, OspfPath>> pathList;

    for (const Vertex& v : spf.confirmedOrder)
    {
        if (v == spf.root)
            continue;

        const auto& node = spf.nodes.at(v);

        if (v.type == VertexType::NETWORK)
        {
            uint16_t type;
            if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV2>)
                type = OSPFV2_LSA_NETWORK;
            else
                type = OSPFV3_LSA_INTRA_AREA_PREFIX;

            LsaKey key = networkLsaKey(v.id, type);
            const auto* nextLsa = lsdb.find(key);
            if (!nextLsa || !std::holds_alternative<NetworkLsa>(nextLsa->body)) continue;
            const NetworkLsa& body = std::get<NetworkLsa>(nextLsa->body);

            auto nextHops = computeNextHops(area, v, spf, nhCache);

            if (nextHops.empty())
                continue;

            if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV2>)
            {
                pathList.emplace_back(
                    IPPrefix{
                        key.linkStateId,
                        static_cast<uint8_t>(std::popcount(body.networkMask))
                    },
                    OspfPath{
                        .area = area.areaId,
                        .type = OspfRouteType::INTRA_AREA,
                        .cost = node.dist,
                        .adminDistance = adminDistance,
                        .nextHops = nextHops
                    });
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV3>)
            {
                for (const auto& prefix : body.prefixes)
                {
                    pathList.emplace_back(
                        prefix.prefix,
                        OspfPath{
                            .options = prefix.options,
                            .area = area.areaId,
                            .type = OspfRouteType::INTRA_AREA,
                            .cost = node.dist + prefix.metric,
                            .adminDistance = adminDistance,
                            .nextHops = nextHops
                        });
                }
            }
            continue;
        }

        if (v.type == VertexType::ROUTER)
        {
            uint16_t type;
            if constexpr (std::is_same_v<std::remove_cv_t<RouterLsa>, RouterLsaV2>)
                type = OSPFV2_LSA_ROUTER;
            else
                type = OSPFV3_LSA_INTRA_AREA_PREFIX;

            LsaAdvKey key(type, static_cast<uint32_t>(v.id));

            auto nextHops = computeNextHops(area, v, spf, nhCache);
            if (nextHops.empty())
                continue;

            if constexpr (std::is_same_v<std::remove_cv_t<RouterLsa>, RouterLsaV2>)
            {
                std::vector<RouterLinkV2> links;

                // Defragment
                lsdb.forEachInAdv(key, [&](uint32_t, const LsaRecord* record) {
                    if (std::holds_alternative<RouterLsaV2>(record->body))
                    {
                        const RouterLsaV2& fragment = std::get<RouterLsaV2>(record->body);
                        for (const auto& link : fragment.links)
                            if (link.type == OSPFV2_LINK_STUB) links.push_back(link);
                    }
                });

                for (const auto& stub : links)
                {
                    if (stub.type != OSPFV2_LINK_STUB) continue;

                    pathList.emplace_back(
                        IPPrefix{
                            stub.linkId,
                            static_cast<uint8_t>(std::popcount(stub.linkData))
                        },
                        OspfPath{
                            .area = area.areaId,
                            .type = OspfRouteType::INTRA_AREA,
                            .cost = stub.metric,
                            .adminDistance = adminDistance,
                            .nextHops = nextHops
                        });
                }
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<RouterLsa>, RouterLsaV3>)
            {
                std::vector<IntraAreaPrefix> prefixes;

                // Defragment
                lsdb.forEachInAdv(key, [&](uint32_t, const LsaRecord* record) {
                    if (std::holds_alternative<IntraAreaPrefixLsa>(record->body))
                    {
                        const IntraAreaPrefixLsa& fragment = std::get<IntraAreaPrefixLsa>(record->body);
                        if (fragment.referencedAdvRouter != key.advertisingRouter || fragment.referencedLsaType != key.lsaType || fragment.referencedLinkStateId != 0)
                            return;
                        prefixes.insert(prefixes.end(), fragment.prefixes.begin(), fragment.prefixes.end());
                    }
                });

                for (const auto& prefix : prefixes)
                {
                    pathList.emplace_back(
                        prefix.prefix,
                        OspfPath{
                            .options = prefix.options,
                            .area = area.areaId,
                            .type = OspfRouteType::INTRA_AREA,
                            .cost = prefix.metric,
                            .adminDistance = adminDistance,
                            .nextHops = nextHops
                        });
                }
            }
        }
    }

    return pathList;
}

template std::vector<std::pair<IPPrefix, OspfPath>> RouteManager::deriveIntraAreaRouters<RouterLsaV2, NetworkLsaV2>(const SpfResult&, OspfArea&);
template std::vector<std::pair<IPPrefix, OspfPath>> RouteManager::deriveIntraAreaRouters<IntraAreaPrefixLsa, IntraAreaPrefixLsa>(const SpfResult&, OspfArea&);
}
