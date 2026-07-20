// IntraRouteManager.cpp

#include "IntraRouteManager.h"
#include "Area.h"
#include "interface/Interface.h"
#include "ospf/OspfProcess.h"
#include "ospf/topology/TopologyTypes.hpp"
#include "ospf/topology/RouteManagerUtility.h"
#include "ospf/spf/SpfTypes.hpp"

namespace routing::ospf
{
IntraRouteManager::IntraRouteManager(Area& a)
    : area(a)
{}

template <typename Policy>
void IntraRouteManager::deriveIntraAreaRoutes(const SpfResult& spf, std::vector<std::pair<types::IPPrefix, OspfPath>>& out)
{
    auto& lsdb = RouteManagerUtility::getLsdb(area);
    const uint8_t adminDistance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::INTRA_AREA_DISTANCE>().load();

    NhCache nhCache;
    out.reserve(out.size() + spf.confirmedOrder.size());

    auto collectIntraAreaPrefixFragments = [&](uint32_t advRouter, uint16_t expectedRefType, uint32_t expectedRefLsId) -> std::vector<IntraAreaPrefix>
    {
        std::vector<IntraAreaPrefix> prefixes;

        const LsaAdvKey searchKey{OSPFV3_LSA_INTRA_AREA_PREFIX, advRouter};
        lsdb.forEachInAdv(searchKey, [&](uint32_t, const LsaRecord& record)
        {
            if (!std::holds_alternative<IntraAreaPrefixLsa>(record.body)) return;

            const IntraAreaPrefixLsa& frag = std::get<IntraAreaPrefixLsa>(record.body);

            if (frag.referencedAdvRouter != advRouter) return;
            if (frag.referencedLsaType != expectedRefType) return;
            if (frag.referencedLinkStateId != expectedRefLsId) return;

            prefixes.insert(prefixes.end(), frag.prefixes.begin(), frag.prefixes.end());
        });

        return prefixes;
    };

    if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::RouterLsa>, RouterLsaV2>)
    {
        const uint32_t selfRid = area.process.getRouterId();
        const LsaKey selfKey(OSPFV2_LSA_ROUTER, selfRid, selfRid);
        auto* selfRec = lsdb.find(selfKey);

        if (selfRec && std::holds_alternative<RouterLsaV2>(selfRec->body))
        {
            auto& ifmgr = RouteManagerUtility::getIfaceMgr(area.process);
            for (const auto& link : std::get<RouterLsaV2>(selfRec->body).links)
            {
                if (link.type != OSPFV2_LINK_STUB) continue;
                
                const OspfInterfaceBase* owner = nullptr;
                ifmgr.forEach([this, &owner, &link](OspfInterfaceId id, const OspfInterfaceBase& iface) {
                    if (id.area != area.areaId) return false;
                    if (iface.getTransmitAddress().v4() == link.linkId)
                    {
                        owner = &iface;
                        return true;
                    }
                    return false;
                });
                if (!owner) continue;

                const uint8_t plen = static_cast<uint8_t>(std::popcount(link.linkData));
                const types::IPPrefix prefix(link.linkId, plen);

                std::vector<OspfNextHop> nextHop{ OspfNextHop{owner->interfaceId, types::IPAddress{}} };

                out.emplace_back(prefix, makePath(
                    area.areaId, 0,
                    adminDistance, link.metric,
                    std::move(nextHop), OspfRouteType::INTRA_AREA
                ));
            }
        }
    }
    else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::RouterLsa>, RouterLsaV3>)
    {
        // The SPF loop below skips spf.root, so derive self's Intra-Area-Prefix-LSA prefixes here directly, mirroring the V2 self-stub-link block above.
        const uint32_t selfRid = area.process.getRouterId();
        auto selfPrefixes = collectIntraAreaPrefixFragments(selfRid, OSPFV3_LSA_ROUTER, 0);

        if (!selfPrefixes.empty())
        {
            auto& ifmgr = RouteManagerUtility::getIfaceMgr(area.process);

            for (const auto& pr : selfPrefixes)
            {
                const OspfInterfaceBase* owner = nullptr;
                ifmgr.forEach([this, &owner, &pr](OspfInterfaceId id, const OspfInterfaceBase& iface) {
                    if (id.area != area.areaId) return false;
                    auto routable = iface.getTransmitInterface()->configs.ipv6.getRoutablePrefixSet(true);
                    if (routable.count(pr.prefix) > 0)
                    {
                        owner = &iface;
                        return true;
                    }
                    return false;
                });
                if (!owner) continue;

                std::vector<OspfNextHop> nextHop{ OspfNextHop{owner->interfaceId, types::IPAddress{}} };

                out.emplace_back(types::IPPrefix(pr.prefix.addr, pr.prefix.prefixLength, true), makePath(
                    area.areaId, pr.options,
                    adminDistance, pr.metric,
                    std::move(nextHop), OspfRouteType::INTRA_AREA
                ));
            }
        }
    }

    for (const Vertex& v : spf.confirmedOrder)
    {
        if (v == spf.root)
            continue;

        const auto& nodeIt = spf.nodes.find(v);
        if (nodeIt == spf.nodes.end()) continue;

        const auto& node = nodeIt->second;

        auto nextHops = RouteManagerUtility::computeNextHops(area, v, spf, nhCache);
        if (nextHops.empty()) continue;

        if (v.type == VertexType::NETWORK)
        {
            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV2>)
            {
                const uint16_t lsaType = OSPFV2_LSA_NETWORK;
                const LsaKey key = networkLsaKey(v.id, lsaType);

                auto* routerLsa = lsdb.find(key);
                if (!routerLsa || !std::holds_alternative<typename Policy::NetworkLsa>(routerLsa->body))
                    continue;

                const auto& body = std::get<NetworkLsaV2>(routerLsa->body);

                const uint8_t plen = static_cast<uint8_t>(std::popcount(body.networkMask));
                const types::IPPrefix prefix{ key.linkStateId, plen };

                out.emplace_back(prefix, makePath(
                        area.areaId, 0,
                        adminDistance, node.dist,
                        std::move(nextHops),
                        OspfRouteType::INTRA_AREA));
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV3>)
            {
                auto prefixes = collectIntraAreaPrefixFragments(
                    networkAdvRouter(v.id), OSPFV3_LSA_NETWORK, networkLsId(v.id));
                for (const auto& pr : prefixes)
                {
                    out.emplace_back(types::IPPrefix(pr.prefix.addr, pr.prefix.prefixLength), makePath(
                            area.areaId, pr.options,
                            adminDistance, node.dist + pr.metric,
                            nextHops, OspfRouteType::INTRA_AREA));
                }
            }
            continue;
        }

        if (v.type == VertexType::ROUTER)
        {
            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::RouterLsa>, RouterLsaV2>)
            {
                const uint16_t lsaType = OSPFV2_LSA_ROUTER;
                const LsaKey key(lsaType, static_cast<uint32_t>(v.id), static_cast<uint32_t>(v.id));

                auto* rec = lsdb.find(key);
                if (!rec || !std::holds_alternative<typename Policy::RouterLsa>(rec->body))
                    continue;

                const auto& links = std::get<typename Policy::RouterLsa>(rec->body).links;

                for (const auto& link : links)
                {
                    if (link.type != OSPFV2_LINK_STUB) continue;

                    const uint8_t plen = static_cast<uint8_t>(std::popcount(link.linkData));
                    const types::IPPrefix prefix{ link.linkId, plen };
                    
                    out.emplace_back(prefix, makePath(
                            area.areaId, 0,
                            adminDistance, node.dist + link.metric,
                            nextHops, OspfRouteType::INTRA_AREA));
                }
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::RouterLsa>, RouterLsaV3>)
            {
                auto prefixes = collectIntraAreaPrefixFragments(
                    static_cast<uint32_t>(v.id), OSPFV3_LSA_ROUTER, 0);
                for (const auto& pr : prefixes)
                {
                    out.emplace_back(types::IPPrefix(pr.prefix.addr, pr.prefix.prefixLength), makePath(
                            area.areaId, pr.options,
                            adminDistance, node.dist + pr.metric,
                            nextHops, OspfRouteType::INTRA_AREA));
                }
            }

            continue;
        }
    }
}

template void IntraRouteManager::deriveIntraAreaRoutes<PolicyV2>(const SpfResult&, std::vector<std::pair<types::IPPrefix, OspfPath>>&);
template void IntraRouteManager::deriveIntraAreaRoutes<PolicyV3>(const SpfResult&, std::vector<std::pair<types::IPPrefix, OspfPath>>&);
}
