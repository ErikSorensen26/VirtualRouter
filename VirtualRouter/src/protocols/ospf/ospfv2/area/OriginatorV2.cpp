// OriginatorV2.cpp

#include <VirtualRouter.h>

#include "OriginatorV2.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"
#include "interface/Interface.h"

namespace OSPF
{
OriginatorV2::OriginatorV2(Area& area) : Originator(area)
{
    initGroupPacing<PolicyV2>();
    auto& configs = area.getConfigs();
    auto type = configs.get<Config::OspfArea::AREA_TYPE>().load();
    if (type == AreaType::TOTALLY_STUB || type == AreaType::TOTALLY_STUB)
        addStubDefaultRoute(true);
    fullRefresh();
}

OriginatorV2::~OriginatorV2()
{
    addStubDefaultRoute(false);
}

void OriginatorV2::fullRefresh()
{
    addRouterLsa(std::nullopt, true, true);

    if (area.type == AreaType::NSSA || area.type == AreaType::TOTALLY_NSSA)
        nssaDefaultOriginate(area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ORIGINATE>().load());
    if (area.type == AreaType::STUB || area.type == AreaType::TOTALLY_STUB)
        addStubDefaultRoute(true);
    
    if (area.type == AreaType::NORMAL)
        for (const auto& asbr : asbrLsas)
            addAsbrLsa(asbr.first, true);
}

void OriginatorV2::updateInterface(uint32_t ifaceId)
{
    addRouterLsa(ifaceId, false);
}

void OriginatorV2::addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh)
{
    uint32_t rid = area.process().getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto& info = originationState[key];
    LsaBody lsa = info.body;
    RouterLsaV2 oldLsa = std::get<RouterLsaV2>(lsa);
    lsa = RouterLsaV2{};
    RouterLsaV2& router = std::get<RouterLsaV2>(lsa);

    router.flags = static_cast<uint8_t>(area.getFlags().getFlags());

    auto& ifmgr = area.process().getIfaceMgr();
    for (auto& [id, iface] : ifmgr.ospfInterfaceList)
    {
        if (id.area != area.areaId) continue;
        addRouterLink(lsa, iface, refresh, fullRefresh || (ifaceId.has_value() && ifaceId.value() == id.interfaceId));
    }
    uniqueLinks(router.links);

    info.refresh = refresh;
    info.expire = router.links.empty();

    if (!refresh && router == oldLsa)
        return;

    lastRouterKey = key;

    processOriginatedLsa<PolicyV2>(key);
}

void OriginatorV2::addNetworkLsa(const OspfInterface& iface, bool refresh)
{
    uint32_t selfRid = area.process().getRouterId();
    uint32_t addr = readU32(iface.interfaceAddress.addr);
    LsaKey key(OSPFV2_LSA_NETWORK, addr, selfRid);

    auto& info = originationState[key];
    LsaBody lsa = info.body;
    NetworkLsaV2 oldLsa = std::get<NetworkLsaV2>(lsa);
    lsa = NetworkLsaV2{};
    NetworkLsaV2& network = std::get<NetworkLsaV2>(lsa);

    bool prefixSuppression = iface.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();

    network.networkMask = prefixSuppression ? 0xFFFFFFFF : iface.interfaceAddress.getMask();
    network.attachedRouters.push_back(selfRid);

    {
        auto& ntable = iface.getNTable();
        for (const auto& [rid, nbr] : ntable.neighbors)
            if (nbr.getState() == Neighbor::State::FULL)
                network.attachedRouters.push_back(rid);
    }
    uniqueLinks(network.attachedRouters);

    info.refresh = refresh;
    
    if (!refresh && oldLsa == network)
        return;

    networkLsas.insert(addr);
    processOriginatedLsa<PolicyV2>(key);
}

void OriginatorV2::addExternal(uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!remove)
    {
        addAsbrLsa(asbr);
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
            expire(asbrLsa);
            externalRoutes.erase(asbr);
            asbrLsas.erase(asbr);
        }
    }
}

void OriginatorV2::originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire)
{
    LsaKey key;

    key.lsaType = OSPFV2_LSA_SUM_NET;
    key.linkStateId = lsid;
    key.advertisingRouter = area.process().getRouterId();

    auto& info = originationState[key];
    LsaBody& body = info.body;
    body = SummaryNetworkLsa{};
    auto& summary = std::get<SummaryNetworkLsa>(body);

    info.expire = expire;

    summary.metric = cost;
    summary.networkMask = prefix.getMask();

    processOriginatedLsa<PolicyV2>(key);
}

void OriginatorV2::translateNssaToExternal(const LsaKey& key7, const LsaBody& body7, bool expire)
{
    if (key7.linkStateId == 0 && std::get<ExternalLsaV2>(body7).networkMask == 0 &&
        !area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ONLY>().load())
        return;

    auto& ext7 = std::get<ExternalLsaV2>(body7);
    auto& base = area.process();
    if (area.process().getConfigs().get<Config::Ospf::LRC_NSSA_TRANSLATION>().load())
    {
        if (ext7.forwardingAddress == 0)
        {
            if (!base.routingInstance->getRib().lookup(key7.linkStateId))
                return;
        }
        else
        {
            if (!base.routingInstance->getRib().lookup(ext7.forwardingAddress))
                return;
        }
    }

    LsaKey key5;

    key5.lsaType = OSPFV2_LSA_EXTERNAL;
    key5.linkStateId = key7.linkStateId;
    key5.advertisingRouter = area.process().getRouterId();

    auto& info = originationState[key5];
    LsaBody& body5 = info.body;
    body5 = body7;

    auto& ext5 = std::get<ExternalLsaV2>(body5);

    if (ext5.forwardingAddress != 0 && !area.isValidForwardAddress(ext5.forwardingAddress))
        ext5.forwardingAddress = 0;

    info.expire = expire;

    processOriginatedLsa<PolicyV2>(key5);
}

void OriginatorV2::addStubDefaultRoute(bool add)
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

    if (!stubDefaultRoute.has_value())
    {
        LsaKey key;
        key.lsaType = OSPFV2_LSA_SUM_NET;
        key.linkStateId = 0;
        key.advertisingRouter = area.process().getRouterId();
        stubDefaultRoute = key;
    }

    auto& info = originationState[stubDefaultRoute.value()];
    LsaBody& body = info.body;
    body = SummaryNetworkLsa{};

    auto& summary = std::get<SummaryNetworkLsa>(body);

    info.expire = !add;

    summary.metric = area.getConfigs().get<Config::OspfArea::DEFAULT_COST>().load();
    summary.networkMask = 0;

    processOriginatedLsa<PolicyV2>(stubDefaultRoute.value());

    if (!add) stubDefaultRoute.reset();
}

void OriginatorV2::expire(LsaKey& key)
{
    originationState[key].expire = true;
    processOriginatedLsa<PolicyV2>(key);
}

void OriginatorV2::addAsbrLsa(uint32_t asbr, bool refresh)
{
    uint32_t selfRid = area.process().getRouterId();
    LsaKey key(OSPFV2_LSA_SUM_ASBR, asbr, selfRid);

    auto& info = originationState[key];
    LsaBody lsa = info.body;
    SummaryRouterLsa oldLsa = std::get<SummaryRouterLsa>(lsa);
    lsa = SummaryRouterLsa{};
    SummaryRouterLsa& asbrLsa = std::get<SummaryRouterLsa>(lsa);

    uint32_t metric = area.process().table.lookupDistance(asbr);
    if (metric == 0) return;

    asbrLsa.metric = metric;

    if (!refresh && oldLsa == asbrLsa)
        return;

    asbrLsas[asbr] = key;
    processOriginatedLsa<PolicyV2>(key);
}

void OriginatorV2::removeNetworkLsa(uint32_t addr)
{
    auto it = networkLsas.find(addr);
    if (it == networkLsas.end()) return;

    LsaKey key = {OSPFV2_LSA_NETWORK, addr, area.process().getRouterId()};

    originationState[key].expire = true;
    processOriginatedLsa<PolicyV2>(key);
    networkLsas.erase(it);
}

void OriginatorV2::addSecondaryLinks(LsaBody& router, const OspfInterface& iface)
{
    auto& lsa = std::get<RouterLsaV2>(router);
    auto secondaries = iface.getIface().configs.ipv4.getSecondaryPrefixList(true);
    for (const auto& secondary : secondaries)
    {
        lsa.links.push_back(RouterLinkV2{
            .linkId = secondary.addr,
            .linkData = Functions::prefixTo32Mask(secondary.prefixLength),
            .type = OSPFV2_LINK_STUB,
            .metric = iface.cost
        });
    }
}

void OriginatorV2::addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr)
{
    (void)nbr; // Used in OSPFv3
    auto& ifaceConfigs = iface.getBaseConfigs();
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = static_cast<uint32_t>(iface.dr.ip.load(std::memory_order_relaxed)),
        .linkData = readU32(iface.interfaceAddress.addr),
        .type = OSPFV2_LINK_TRANSIT,
        .metric = iface.cost
    });
    if (ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void OriginatorV2::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    auto& ifaceConfigs = iface.getBaseConfigs();
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = neighbor.routerID,
        .linkData = readU32(iface.interfaceAddress.addr),
        .type = OSPFV2_LINK_P2P,
        .metric = iface.cost
    });
    if (ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void OriginatorV2::addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask)
{
    auto& ifaceConfigs = iface.getBaseConfigs();
    auto& cost = iface.getConfigs().get<Config::OspfInterface::COST>();
    uint16_t metric = area.process().getConfigs().get<Config::Ospf::MAX_METRIC_INCLUDE_STUB>().load()
        ? 0xFFFF : cost.hasValue() ? cost.load() : iface.cost;
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = readU32(iface.interfaceAddress.addr),
        .linkData = fullMask ? 0xFFFFFFFF : Functions::prefixTo32Mask(iface.interfaceAddress.prefixLength),
        .type = OSPFV2_LINK_STUB,
        .metric = metric
    });
    if (ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void OriginatorV2::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = vNbr.routerID,
        .linkData = iface.getAreaId(),
        .type = OSPFV2_LINK_VIRTUAL,
        .metric = iface.cost
    });
}
}
