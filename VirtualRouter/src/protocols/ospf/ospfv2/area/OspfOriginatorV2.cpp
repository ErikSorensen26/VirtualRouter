// OspfOriginatorV2.cpp

#include "OspfOriginatorV2.h"
#include <OspfNeighbor.h>
#include <OspfInterface.h>
#include <OspfArea.h>
#include <OspfTopology.h>
#include <OspfProcess.h>

namespace OSPF
{
OspfOriginatorV2::OspfOriginatorV2(OspfArea& area) : OspfOriginator(area) {}

void OspfOriginatorV2::updateInterface(uint32_t ifaceId)
{
    addRouterLsa(ifaceId);
    area.flood();
}

void OspfOriginatorV2::addRouterLsa(uint32_t ifaceId)
{
    LsaBody lsa = RouterLsaV2{};
    RouterLsaV2& router = std::get<RouterLsaV2>(lsa);

    router.flags = static_cast<uint8_t>(area.getFlags().getFlags());

    {
        auto& ifmgr = area.topology().process.getIfaceMgr();
        std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
        for (auto& [id, iface] : ifmgr.ospfInterfaceList)
        {
            if (id.area != area.areaId) continue;
            addRouterLink(lsa, iface, id.interfaceId == ifaceId);
        }
    }
    uniqueLinks(router.links);

    uint32_t rid = area.topology().process.getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    if (lastRouterLsa.has_value() && (std::get<RouterLsaV2>(lastRouterLsa.value()) == router || key != lastRouterKey))
        return;

    lastRouterLsa = lsa;
    lastRouterKey = key;

    area.processReoriginatedLsa<PolicyV2>(key, std::move(lsa));
}

void OspfOriginatorV2::addNetworkLsa(const OspfInterface& iface)
{
    LsaBody lsa = NetworkLsaV2{};
    NetworkLsaV2& network = std::get<NetworkLsaV2>(lsa);

    uint32_t selfRid = area.topology().process.getRouterId();
    network.networkMask = iface.interfaceAddress.getMask();
    network.attachedRouters.push_back(selfRid);

    {
        auto& ntable = iface.getNTable();
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        for (const auto& [rid, nbr] : ntable.neighbors)
            if (nbr.getState() == Neighbor::State::FULL)
                network.attachedRouters.push_back(rid);
    }

    uniqueLinks(network.attachedRouters);

    LsaKey key(OSPFV2_LSA_NETWORK, readU32(iface.interfaceAddress.addr), selfRid);
    
    auto it = networkLsas.find(iface.id);
    if (it != networkLsas.end() && std::get<NetworkLsaV2>(it->second.lastLsa) == network && it->second.key == key)
        return;

    networkLsas[iface.id] = NetworkState{key, lsa};
    area.processReoriginatedLsa<PolicyV2>(key, std::move(lsa));
}

void OspfOriginatorV2::addExternal(uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!asbrExternalRouters.contains(asbr))
    {
        if (remove) return;
        addAsbrLsa(asbr);
    }

    auto& external = asbrExternalRouters[asbr];

    bool found = std::find(external.second.begin(), external.second.end(), lsid) != external.second.end();

    if (!found && !remove)
    {
        external.second.push_back(lsid);
    }
    else if (found && remove)
    {
        external.second.erase(std::find(external.second.begin(), external.second.end(), lsid));
        if (external.second.empty())
        {
            LsaKey key{OSPFV2_LSA_SUM_ASBR, asbr, area.topology().process.getRouterId()};
            expire(key, external.first);
            asbrExternalRouters.erase(asbr);
        }
    }
}

void OspfOriginatorV2::expire(LsaKey& key, LsaBody& lsa)
{
    area.processReoriginatedLsa<PolicyV2>(key, std::move(lsa), true);
}

void OspfOriginatorV2::addAsbrLsa(uint32_t asbr)
{
    uint32_t metric = area.topology().table.lookupDistance(asbr);
    if (metric == 0) return;
    auto it = asbrExternalRouters.emplace(asbr, SummaryRouterLsa{}, std::vector<uint32_t>{});
    if (!it.second) return;

    auto& lsa = std::get<SummaryRouterLsa>(it.first->second.first);
    lsa.metric = metric;

    LsaKey key(OSPFV3_LSA_INTER_AREA_ROUTER, asbr, area.topology().process.getRouterId());

    area.processReoriginatedLsa<PolicyV2>(key, lsa);
}

void OspfOriginatorV2::removeNetworkLsa(uint32_t ifaceId)
{
    OspfInterfaceId id(ifaceId, area.areaId);
    auto it = networkLsas.find(id);
    if (it == networkLsas.end()) return;

    area.processReoriginatedLsa<PolicyV2>(it->second.key, std::move(it->second.lastLsa), true);
    networkLsas.erase(it);
}

void OspfOriginatorV2::addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr)
{
    (void)nbr; // Used in OSPFv3
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = static_cast<uint32_t>(iface.dr.ip.load(std::memory_order_relaxed)),
        .linkData = readU32(iface.interfaceAddress.addr),
        .type = OSPFV2_LINK_TRANSIT,
        .metric = iface.configs->cost.load(std::memory_order_relaxed)
    });
}

void OspfOriginatorV2::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = neighbor.routerID,
        .linkData = readU32(iface.interfaceAddress.addr),
        .type = OSPFV2_LINK_P2P,
        .metric = iface.configs->cost.load(std::memory_order_relaxed)
    });
}

void OspfOriginatorV2::addStubLink(LsaBody& router, const OspfInterface& iface)
{
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = readU32(iface.interfaceAddress.addr),
        .linkData = Functions::prefixTo32Mask(iface.interfaceAddress.prefixLength),
        .type = OSPFV2_LINK_STUB,
        .metric = iface.configs->cost.load(std::memory_order_relaxed)
    });
}

void OspfOriginatorV2::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = vNbr.routerID,
        .linkData = iface.getAreaId(),
        .type = OSPFV2_LINK_VIRTUAL,
        .metric = iface.configs->cost.load(std::memory_order_relaxed)
    });
}
}
