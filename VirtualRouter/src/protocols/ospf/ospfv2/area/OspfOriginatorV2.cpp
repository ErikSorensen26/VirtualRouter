// OspfOriginatorV2.cpp

#include "OspfOriginatorV2.h"
#include <OspfNeighbor.h>
#include <OspfInterface.h>
#include <OspfArea.h>
#include <OspfProcess.h>
#include <Interface.h>

namespace OSPF
{
OspfOriginatorV2::OspfOriginatorV2(OspfArea& area) : OspfOriginator(area)
{
    fullRefresh();
}

void OspfOriginatorV2::fullRefresh()
{
    RefreshInfo info{.isRefresh = true};
    addRouterLsa(std::nullopt, info, true);

    if (area.type == AreaType::NSSA || area.type == AreaType::TOTALLY_NSSA)
        nssaDefaultOriginate(area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ORIGINATE>().load());
    if (area.type == AreaType::STUB || area.type == AreaType::TOTALLY_STUB)
        addStubDefaultRoute(true);
    
    if (area.type == AreaType::NORMAL)
        for (const auto& asbr : asbrLsas)
            addAsbrLsa(asbr.first, info);

    startRefresh<PolicyV2>(info);
    area.flood<PolicyV2>();
}

void OspfOriginatorV2::updateInterface(uint32_t ifaceId)
{
    RefreshInfo info{};
    addRouterLsa(ifaceId, info);
    startRefresh<PolicyV2>(info);
    area.flood<PolicyV2>();
}

void OspfOriginatorV2::addRouterLsa(std::optional<uint32_t> ifaceId, RefreshInfo& refresh, bool fullRefresh)
{
    LsaBody lsa = RouterLsaV2{};
    RouterLsaV2& router = std::get<RouterLsaV2>(lsa);

    router.flags = static_cast<uint8_t>(area.getFlags().getFlags());

    {
        auto& ifmgr = area.process().getIfaceMgr();
        std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
        for (auto& [id, iface] : ifmgr.ospfInterfaceList)
        {
            if (id.area != area.areaId) continue;
            addRouterLink(lsa, iface, refresh, fullRefresh || (ifaceId.has_value() && ifaceId.value() == id.interfaceId));
        }
    }
    uniqueLinks(router.links);

    uint32_t rid = area.process().getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    if (!refresh.isRefresh && lastRouterLsa.has_value() && std::get<RouterLsaV2>(lastRouterLsa.value()) == router && key == lastRouterKey)
        return;

    lastRouterLsa = lsa;
    lastRouterKey = key;

    processOriginatedLsa<PolicyV2>(key, lsa, false, &refresh);
}

void OspfOriginatorV2::addNetworkLsa(const OspfInterface& iface, RefreshInfo& refresh)
{
    LsaBody lsa = NetworkLsaV2{};
    NetworkLsaV2& network = std::get<NetworkLsaV2>(lsa);

    bool prefixSuppression = iface.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();

    uint32_t selfRid = area.process().getRouterId();
    network.networkMask = prefixSuppression ? 0xFFFFFFFF : iface.interfaceAddress.getMask();
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
    if (!refresh.isRefresh && it != networkLsas.end() && std::get<NetworkLsaV2>(it->second.lastLsa) == network && it->second.key == key)
        return;

    networkLsas[iface.id] = LsaState{key, lsa};
    processOriginatedLsa<PolicyV2>(key, lsa, false, &refresh);
}

void OspfOriginatorV2::addExternal(uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!remove)
    {
        RefreshInfo info{};
        addAsbrLsa(asbr, info);
        startRefresh<PolicyV2>(info);
    }

    auto& external = externalRoutes[asbr];

    bool found = std::find(external.begin(), external.end(), lsid) != external.end();

    if (!found && !remove)
    {
        external.push_back(lsid);
    }
    else if (found && remove)
    {
        external.erase(std::find(external.begin(), external.end(), lsid));
        if (external.empty())
        {
            auto& asbrLsa = asbrLsas[asbr];
            expire(asbrLsa.key, asbrLsa.lastLsa);
            externalRoutes.erase(asbr);
            asbrLsas.erase(asbr);
        }
    }
}

void OspfOriginatorV2::originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire)
{
    LsaKey key;
    LsaBody body = SummaryNetworkLsa();

    auto& summary = std::get<SummaryNetworkLsa>(body);

    key.lsaType = OSPFV2_LSA_SUM_NET;
    key.linkStateId = lsid;
    key.advertisingRouter = area.process().getRouterId();

    summary.metric = cost;
    summary.networkMask = prefix.getMask();

    processOriginatedLsa<PolicyV2>(key, body, expire);
}

void OspfOriginatorV2::translateNssaToExternal(const LsaKey& key7, const LsaBody& body7, bool expire)
{
    if (key7.linkStateId == 0 && std::get<ExternalLsaV2>(body7).networkMask == 0 &&
        !area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ONLY>().load())
        return;

    LsaKey key5;
    LsaBody body5 = body7;

    auto& ext5 = std::get<ExternalLsaV2>(body5);

    key5.lsaType = OSPFV2_LSA_EXTERNAL;
    key5.linkStateId = key7.linkStateId;
    key5.advertisingRouter = area.process().getRouterId();

    if (ext5.forwardingAddress != 0 && !area.isValidForwardAddress(ext5.forwardingAddress))
        ext5.forwardingAddress = 0;

    processOriginatedLsa<PolicyV2>(key5, body5, expire);
}

void OspfOriginatorV2::addStubDefaultRoute(bool add)
{
    if (area.type != AreaType::STUB && area.type != AreaType::TOTALLY_STUB)
        return;
    if (stubDefaultRoute.has_value() == add)
        return;
    if (!area.process().isABR() && add)
    {
        if (stubDefaultRoute)
            addStubDefaultRoute(false);
        return;
    }

    LsaBody body = SummaryNetworkLsa();

    auto& summary = std::get<SummaryNetworkLsa>(body);

    if (!stubDefaultRoute.has_value())
    {
        LsaKey key;
        key.lsaType = OSPFV2_LSA_SUM_NET;
        key.linkStateId = 0;
        key.advertisingRouter = area.process().getRouterId();
        stubDefaultRoute = key;
    }

    summary.metric = area.getConfigs().get<Config::OspfArea::DEFAULT_COST>().load();
    summary.networkMask = 0;

    processOriginatedLsa<PolicyV2>(stubDefaultRoute.value(), body, !add);

    if (!add) stubDefaultRoute.reset();
}

void OspfOriginatorV2::expire(LsaKey& key, LsaBody& lsa)
{
    processOriginatedLsa<PolicyV2>(key, lsa, true, nullptr);
}

void OspfOriginatorV2::addAsbrLsa(uint32_t asbr, RefreshInfo& refresh)
{
    uint32_t metric = area.process().table.lookupDistance(asbr);
    if (metric == 0) return;
    auto it = asbrLsas.find(asbr);

    LsaBody lsa = SummaryNetworkLsa{};
    auto& asbrLsa = std::get<SummaryRouterLsa>(lsa);
    asbrLsa.metric = metric;

    LsaKey key(OSPFV3_LSA_INTER_AREA_ROUTER, asbr, area.process().getRouterId());

    if (!refresh.isRefresh && it != asbrLsas.end() && std::get<SummaryRouterLsa>(it->second.lastLsa) == asbrLsa && key == it->second.key)
        return;

    asbrLsas[asbr] = LsaState{key, lsa};
    processOriginatedLsa<PolicyV2>(key, lsa, false, &refresh);
}

void OspfOriginatorV2::removeNetworkLsa(uint32_t ifaceId)
{
    OspfInterfaceId id(ifaceId, area.areaId);
    auto it = networkLsas.find(id);
    if (it == networkLsas.end()) return;

    processOriginatedLsa<PolicyV2>(it->second.key, it->second.lastLsa, true, nullptr);
    networkLsas.erase(it);
}

void OspfOriginatorV2::addSecondaryLinks(LsaBody& router, const OspfInterface& iface)
{
    auto& lsa = std::get<RouterLsaV2>(router);
    auto secondaries = iface.getIface().configs.ipv4.getSecondaryPrefixList(true);
    for (const auto& secondary : secondaries)
    {
        lsa.links.push_back(RouterLinkV2{
            .linkId = secondary.addr,
            .linkData = Functions::prefixTo32Mask(secondary.prefixLength),
            .type = OSPFV2_LINK_STUB,
            .metric = iface.getConfigs().get<Config::OspfInterface::COST>().load()
        });
    }
}

void OspfOriginatorV2::addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr)
{
    (void)nbr; // Used in OSPFv3
    auto& ifaceConfigs = iface.getBaseConfigs();
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = static_cast<uint32_t>(iface.dr.ip.load(std::memory_order_relaxed)),
        .linkData = readU32(iface.interfaceAddress.addr),
        .type = OSPFV2_LINK_TRANSIT,
        .metric = iface.getConfigs().get<Config::OspfInterface::COST>().load()
    });
    if (ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void OspfOriginatorV2::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    auto& ifaceConfigs = iface.getBaseConfigs();
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = neighbor.routerID,
        .linkData = readU32(iface.interfaceAddress.addr),
        .type = OSPFV2_LINK_P2P,
        .metric = iface.getConfigs().get<Config::OspfInterface::COST>().load()
    });
    if (ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void OspfOriginatorV2::addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask)
{
    auto& ifaceConfigs = iface.getBaseConfigs();
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = readU32(iface.interfaceAddress.addr),
        .linkData = fullMask ? 0xFFFFFFFF : Functions::prefixTo32Mask(iface.interfaceAddress.prefixLength),
        .type = OSPFV2_LINK_STUB,
        .metric = iface.getConfigs().get<Config::OspfInterface::COST>().load()
    });
    if (ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void OspfOriginatorV2::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = vNbr.routerID,
        .linkData = iface.getAreaId(),
        .type = OSPFV2_LINK_VIRTUAL,
        .metric = iface.getConfigs().get<Config::OspfInterface::COST>().load()
    });
}
}
