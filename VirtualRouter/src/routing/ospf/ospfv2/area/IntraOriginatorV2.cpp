// OriginatorV2.cpp

#include <RCU.hpp>
#include <VirtualRouter.h>

#include "IntraOriginatorV2.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"
#include "interface/Interface.h"

namespace routing::ospf
{
IntraOriginatorV2::IntraOriginatorV2(OriginatorContext& ctx) : IntraOriginator(ctx)
{
    ctx.initGroupPacing<PolicyV2>();
    ctx.getInterOriginator().refreshStubDefaultOriginate<PolicyV2>(ctx);
    fullRefresh();
}

IntraOriginatorV2::~IntraOriginatorV2()
{
    context.getInterOriginator().setStubDefaultOriginate<PolicyV2>(context, false);
}

void IntraOriginatorV2::fullRefresh()
{
    addRouterLsa(std::nullopt, true, true);
    context.getInterOriginator().refreshStubDefaultOriginate<PolicyV2>(context);
    context.getInterOriginator().refreshNssaDefaultOriginate(context);
    context.getInterOriginator().refreshAsbrs<PolicyV2>(context);
}

void IntraOriginatorV2::updateInterface(uint32_t ifaceId)
{
    addRouterLsa(ifaceId, false);
}

void IntraOriginatorV2::addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh)
{
    uint32_t rid = context.area.process.getRouterId();
    LsaKey key(OSPFV2_LSA_ROUTER, rid, rid);

    auto& info = context.originationState[key];
    LsaBody lsa = info.body;
    RouterLsaV2 oldLsa = std::holds_alternative<std::monostate>(lsa)
        ? RouterLsaV2{} : std::get<RouterLsaV2>(lsa);
    lsa = RouterLsaV2{};
    RouterLsaV2& router = std::get<RouterLsaV2>(lsa);

    router.flags = static_cast<uint8_t>(context.getAreaFlags());

    auto& ifmgr = context.getIfaceMgr();
    ifmgr.forEach([&](OspfInterfaceId id, const OspfInterface& iface) {
        if (id.area != context.area.areaId) return;
        addRouterLink(lsa, iface, refresh, fullRefresh || (ifaceId.has_value() && ifaceId.value() == id.interfaceId));
    });
    uniqueLinks(router.links);

    info.refresh = refresh;
    info.expire = router.links.empty();

    if (!refresh && router == oldLsa)
        return;

    lastRouterKey = key;

    context.processOriginatedLsa<PolicyV2>(key);
}

void IntraOriginatorV2::addNetworkLsa(const OspfInterface& iface, bool refresh)
{
    uint32_t selfRid = context.area.process.getRouterId();
    uint32_t addr = iface.interfaceAddress.v4();
    LsaKey key(OSPFV2_LSA_NETWORK, addr, selfRid);

    auto& info = context.originationState[key];
    LsaBody& lsa = info.body;
    auto* oldPtr = std::get_if<NetworkLsaV2>(&lsa);
    std::optional<NetworkLsaV2> oldLsa;
    if (oldPtr) oldLsa = *oldPtr;
    lsa = NetworkLsaV2{};
    NetworkLsaV2& network = std::get<NetworkLsaV2>(lsa);

    const InterfaceManager& ifaceMgr = context.getIfaceMgr();
    bool prefixSuppression = ifaceMgr.getInterfaceBaseConfigs(iface).get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();

    network.networkMask = prefixSuppression ? 0xFFFFFFFF : iface.interfaceAddress.getMask();
    network.attachedRouters = ifaceMgr.getNTable(iface).getNeighborRIDs();

    uniqueLinks(network.attachedRouters);

    info.refresh = refresh;

    if (!refresh && oldLsa && *oldLsa == network)
        return;

    networkLsas.insert(addr);
    context.processOriginatedLsa<PolicyV2>(key);
}

void IntraOriginatorV2::expire(LsaKey& key)
{
    context.originationState[key].expire = true;
    context.processOriginatedLsa<PolicyV2>(key);
}

void IntraOriginatorV2::removeNetworkLsa(uint32_t addr)
{
    auto it = networkLsas.find(addr);
    if (it == networkLsas.end()) return;

    LsaKey key = {OSPFV2_LSA_NETWORK, addr, context.area.process.getRouterId()};

    context.originationState[key].expire = true;
    context.processOriginatedLsa<PolicyV2>(key);
    networkLsas.erase(it);
}

void IntraOriginatorV2::addSecondaryLinks(LsaBody& router, const OspfInterface& iface)
{
    auto& lsa = std::get<RouterLsaV2>(router);
    auto secondaries = iface.iface.configs.ipv4.getSecondaryPrefixList(true);
    for (const auto& secondary : secondaries)
    {
        lsa.links.push_back(RouterLinkV2{
            .linkId = secondary.addr,
            .linkData = types::v4Mask(secondary.prefixLength),
            .type = OSPFV2_LINK_STUB,
            .metric = iface.getCost()
        });
    }
}

void IntraOriginatorV2::addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr)
{
    (void)nbr; // Used in OSPFv3
    auto& ifaceConfigs = context.getIfaceMgr().getInterfaceBaseConfigs(iface);
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = static_cast<uint32_t>(iface.getDrIp()),
        .linkData = iface.interfaceAddress.v4(),
        .type = OSPFV2_LINK_TRANSIT,
        .metric = iface.getCost()
    });
    if (ifaceConfigs.get<config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void IntraOriginatorV2::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    auto& ifaceConfigs = context.getIfaceMgr().getInterfaceBaseConfigs(iface);
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = neighbor.routerID,
        .linkData = iface.interfaceAddress.v4(),
        .type = OSPFV2_LINK_P2P,
        .metric = iface.getCost()
    });
    if (ifaceConfigs.get<config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void IntraOriginatorV2::addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask)
{
    auto& ifaceConfigs = context.getIfaceMgr().getInterfaceBaseConfigs(iface);
    auto cost = ifaceConfigs.get<config::OspfInterfaceBase::BASE>().get().get<config::OspfInterface::COST>();
    uint16_t metric = context.getProcessConfigs().get<config::Ospf::MAX_METRIC_INCLUDE_STUB>().load()
        ? 0xFFFF : cost.hasValue() ? cost.load() : iface.getCost();
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = iface.interfaceAddress.v4(),
        .linkData = fullMask ? 0xFFFFFFFF : types::v4Mask(iface.interfaceAddress.prefixLength),
        .type = OSPFV2_LINK_STUB,
        .metric = metric
    });
    if (ifaceConfigs.get<config::OspfInterfaceBase::INCLUDE_SECONDARIES>().load() &&
        ifaceConfigs.get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
        addSecondaryLinks(router, iface);
}

void IntraOriginatorV2::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV2>(router).links.push_back(RouterLinkV2{
        .linkId = vNbr.routerID,
        .linkData = iface.getAreaId(),
        .type = OSPFV2_LINK_VIRTUAL,
        .metric = iface.getCost()
    });
}
} // namespace routing
