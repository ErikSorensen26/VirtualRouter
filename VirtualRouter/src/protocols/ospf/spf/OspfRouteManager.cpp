// OspfRouteManager.cpp

#include "OspfRouteManager.h"
#include <OspfProcess.h>
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
RouteManager::RouteManager(OspfProcess& proc)
    : process(proc), rib(proc.getRib()), af(proc.getAF()) {}

std::vector<OspfNextHop> RouteManager::computeNextHops(uint32_t area, const Vertex& v, const SpfResult& spf, NhCache& cache)
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

void RouteManager::dedupe(std::vector<OspfNextHop>& hops)
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

std::optional<OspfNextHop> RouteManager::resolveDirectNextHop(uint32_t area, const Vertex& v, const ParentRef& pref)
{
    uint32_t rid = v.type == VertexType::ROUTER
        ? static_cast<uint32_t>(v.id)
        : networkAdvRouter(v.id);

    auto& ifaceMgr = process.getIfaceMgr();

    OspfInterface* iface = ifaceMgr.getInterface({pref.ifid, area});
    if (!iface) return std::nullopt;

    Neighbor* nbr = iface->getNTable().lookup(rid);
    if (!nbr) return std::nullopt;

    return OspfNextHop{pref.ifid, nbr->ipAddress};
}

template <typename NetworkLsa, typename RouterLsa>
void RouteManager::deriveIntraAreaRouters(const SpfResult& spf, const OspfArea& area)
{
    auto& lsdb = area.lsdb();

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
            const auto& nextLsa = lsdb.find(key);
            if (!std::holds_alternative<NetworkLsa>(nextLsa->body)) continue;
            const NetworkLsa& body = std::get<NetworkLsa>(nextLsa->body);

            auto nextHops = computeNextHops(area.areaId, v, spf, nhCache);

            if constexpr (std::is_same_v<std::remove_cv_t<NetworkLsa>, NetworkLsaV2>)
            {
                pathList.emplace_back(
                    IPPrefix{
                        key.linkStateId,
                        std::popcount(body.networkMask)
                    },
                    OspfPath{
                        .area = area.areaId,
                        .type = OspfRouteType::INTRA_AREA,
                        .cost = node.dist,
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
                            .area = area.areaId,
                            .type = OspfRouteType::INTRA_AREA,
                            .cost = prefix.metric,
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

            LsaKey key = routerLsakey(v.id, type);
            const auto& nextLsa = lsdb.find(key);
            if (!std::holds_alternative<RouterLsa>(nextLsa->body)) continue;
            const RouterLsa& body = std::get<RouterLsa>(nextLsa->body);

            auto nextHops = computeNextHops(area.areaId, v, spf, nhCache);

            if constexpr (std::is_same_v<std::remove_cv_t<RouterLsa>, RouterLsaV2>)
            {
                for (const auto& stub : body.links)
                {
                    if (stub.type != OSPFV2_LINK_STUB) continue;

                    pathList.emplace_back(
                        IPPrefix{
                            stub.linkId,
                            std::popcount(stub.linkData)
                        },
                        OspfPath{
                            .area = area.areaId,
                            .type = OspfRouteType::INTRA_AREA,
                            .cost = stub.metric,
                            .nextHops = nextHops
                        });
                }
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<RouterLsa>, RouterLsaV3>)
            {
                for (const auto& prefix : body.prefixes)
                {
                    pathList.emplace_back(
                        prefix.prefix,
                        OspfPath{
                            .area = area.areaId,
                            .type = OspfRouteType::INTRA_AREA,
                            .cost = prefix.metric,
                            .nextHops = nextHops
                        });
                }
            }
        }
    }

    rib.replaceArea(area.areaId, pathList);
}

template void RouteManager::deriveIntraAreaRouters<RouterLsaV2, NetworkLsaV2>(const SpfResult&, const OspfArea&);
template void RouteManager::deriveIntraAreaRouters<IntraAreaPrefixLsa, IntraAreaPrefixLsa>(const SpfResult&, const OspfArea&);
}
