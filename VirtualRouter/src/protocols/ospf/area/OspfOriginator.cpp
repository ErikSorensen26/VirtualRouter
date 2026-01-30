// OspfOriginator.cpp

#include "OspfOriginator.h"
#include <OspfArea.h>
#include <OspfProcess.h>
#include <OspfInterface.h>
#include <OspfNeighbor.h>
#include <TimeManager.h>
#include <VirtualRouter.h>

#include <Interface.h>
#include <InterfaceType.hpp>

namespace OSPF
{
OspfOriginator::OspfOriginator(OspfArea& a) : area(a), tmgr(area.process().tmgr) {}

OspfOriginator::~OspfOriginator()
{
    nssaDefaultOriginate(false);
    addStubDefaultRoute(false);
    addDefaultRoute(false);

    for (const auto& [tid, _] : refreshTimers)
        tmgr.cancelTimer(tid);
    refreshTimers.clear();
}

template <typename Policy>
void OspfOriginator::startRefresh(RefreshInfo& info)
{
    if (info.keys.empty()) return; // No keys refreshed, no need to start another timer.

    if (info.activeTimerId.has_value()) // Id from prevous refresh
        refreshTimers.erase(info.activeTimerId.value());

    std::chrono::steady_clock::time_point expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(OSPF_REFRESH_AGE);
    uint32_t tid = tmgr.addTimer(expireTime, [this](uint32_t tid) {
        handleRefreshTimeout<Policy>(tid);
    });

    refreshTimers[tid] = std::move(info.keys);
}

template <typename Policy>
void OspfOriginator::handleRefreshTimeout(uint32_t tid)
{
    auto refresh = refreshTimers.find(tid);
    if (refresh == refreshTimers.end()) return;

    constexpr uint16_t routerType = std::is_same_v<typename Policy::RouterLsa, RouterLsaV2>
        ? OSPFV2_LSA_ROUTER : OSPFV3_LSA_ROUTER;
    constexpr uint16_t networkType = std::is_same_v<typename Policy::NetworkLsa, NetworkLsaV2>
        ? OSPFV2_LSA_NETWORK : OSPFV3_LSA_NETWORK;
    constexpr uint16_t asbrType = std::is_same_v<typename Policy::InterRouterLsa, InterAreaRouterLsa>
        ? OSPFV2_LSA_SUM_ASBR : OSPFV3_LSA_INTER_AREA_ROUTER;

    auto& ifaceMgr = area.process().getIfaceMgr();
    RefreshInfo info{.isRefresh = true, .activeTimerId = tid};

    bool processRouter = false;

    for (const auto& key : refresh->second)
    {
        switch (key.lsaType)
        {
            case routerType:
            {
                if (!processRouter)
                {
                    if (processRouter) addRouterLsa(std::nullopt, info);
                    processRouter = true;
                }
                break;
            }
            case networkType:
            {
                auto* iface = ifaceMgr.getInterface(OspfInterfaceId{key.linkStateId, area.areaId});
                if (!iface) break;
                addNetworkLsa(*iface, info);
                break;
            }
            case asbrType:
            {
                addAsbrLsa(key.linkStateId, info);
            }
        }
    }

    startRefresh<Policy>(info);
    area.flood<Policy>();
}

void OspfOriginator::addRouterLink(LsaBody& router, const OspfInterface& iface, RefreshInfo& refresh, bool attemptNetLsa)
{
    if (iface.getAreaId() != area.areaId) return;

    auto& ifaceConfigs = iface.getConfigs();

    bool prefixSuppression = iface.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();
    auto ntype = ifaceConfigs.get<Config::OspfInterface::NETWORK>().load();
    const auto& ntable = iface.getNTable();

    if (ifaceConfigs.get<Config::OspfInterface::PASSIVE>().load() ||
        iface.getIface().configs.interfaceType == InterfaceType::LOOPBACK)
    {
        addStubLink(router, iface);
        return;
    }

    if (ntype == NetworkType::POINT_TO_POINT)
    {
        bool isVirtual = iface.isVirtual.load(std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> nlock(ntable.mu);
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() != Neighbor::State::FULL)
                continue;

            if (isVirtual)
                addVirtualLink(router, iface, nbr);
            else
            {
                addP2PLink(router, iface, nbr);
                if (!prefixSuppression)
                    addStubLink(router, iface);
            }
        }
        return;
    }

    if (ntype == NetworkType::POINT_TO_MULTIPOINT)
    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() != Neighbor::State::FULL)
                continue;

            addP2PLink(router, iface, nbr);
            if (!prefixSuppression)
                addStubLink(router, iface, true);
        }
    }

    if (ntype == NetworkType::BROADCAST ||
        ntype == NetworkType::NON_BROADCAST)
    {
        bool isDr = iface.isDr.load(std::memory_order_relaxed);
        bool haveDr = isDr;

        const Neighbor* drNbr = nullptr;
        if (!haveDr)
        {
            drNbr = ntable.lookup(iface.dr.rid.load(std::memory_order_relaxed));
            if (drNbr && drNbr->getState() == Neighbor::State::FULL)
                haveDr = true;
        }

        // ALL routers advertise a transit link if the network is operational
        if (haveDr)
        {
            addTransitLink(router, iface, drNbr);
            if (isDr)
            {
                if (attemptNetLsa)
                {
                    addNetworkLsa(iface, refresh);
                    return;
                }
            }
        }

        return;
    }
}

template <typename Policy>
void OspfOriginator::processReoriginatedLsa(const LsaKey& key, LsaBody& body, bool refresh, bool expire)
{
    LsaHeader hdr{};
    IncomingLsaContext ctx = {
        .key = key,
        .header = hdr
    };

    LsaRecord* existing = area.lsdb().find(ctx.key);

    if constexpr (std::is_same_v<Policy, PolicyV2>)
    {
        uint32_t options = area.getFlags().getFlags();
        if (key.linkStateId == OSPFV2_LSA_NSSA)
            InterfaceFlagManager::setPropagate(options, true);
        InterfaceFlagManager::setDemandCircuits(options, true);
        ctx.header.options = static_cast<uint8_t>(options);
    }
    ctx.selfOriginatedKey = true;
    ctx.header.age = expire ? OSPF_MAX_AGE : 0;
    FloodReason reason = expire ? FloodReason::FLUSH : refresh ? FloodReason::REFRESH : FloodReason::UPDATE;
    ctx.info = {.reason = reason};
    ctx.header.sequence = existing
        ? existing->header.sequence + 1 : 0;

    auto results = runLsaCalculations<Policy>(ctx.header, key, body);

    ctx.header.length = results.size;
    ctx.header.checksum = results.checksum;

    (void)area.processLsa<Policy>(ctx, body);
}

template <typename Policy>
void OspfOriginator::processOriginatedLsa(const LsaKey& key, LsaBody& body, bool expire, RefreshInfo* refresh)
{
    processReoriginatedLsa<Policy>(key, body, refresh == nullptr, expire);

    if (refresh)
        refresh->keys.push_back(key);
    else
        lsaRefreshes.erase(key);
}

bool OspfOriginator::isValidForwardAddress(const std::optional<IPAddress>& nh) const
{
    if (!nh.has_value() || area.getConfigs().get<Config::OspfArea::NSSA_SUPPRESS_FA>().load())
        return false;

    const OspfInterface* iface = area.process().getIfaceMgr().getInterfaceByAddress(nh.value());
    if (!iface) return false;

    if (iface->getAreaId() != area.areaId)
        return false;

    auto& rib = area.process().routingInstance->routingTable;
    if (nh->isV6)
    {
        if (auto* entry = rib.lookup(readU128(nh->raw)); !entry || entry->source != RouteSource::OSPF_INTRA)
            return false;
    }
    else
    {
        if (auto* entry = rib.lookup(readU32(nh->raw)); !entry || entry->source != RouteSource::OSPF_INTRA)
            return false;
    }

    return true;
}

void OspfOriginator::nssaDefaultOriginate(bool add)
{
    if (area.type != AreaType::NSSA && area.type != AreaType::TOTALLY_NSSA)
        return;
    if (nssaDefaultRoute.has_value() == add)
        return;
    if (!area.process().isABR() && add)
    {
        if (nssaDefaultRoute)
            nssaDefaultOriginate(false);
        return;
    }

    auto& configs = area.getConfigs();
    ExternalOriginateContext ctx = {
        .lsId = nssaDefaultRoute.has_value() ? std::optional{nssaDefaultRoute->linkStateId} : std::nullopt,
        .prefix = IPPrefix(area.process().getAF()),
        .metric = configs.get<Config::OspfArea::NSSA_DEFAULT_METRIC>().load(),
        .tag = 0,
        .nextHop = IPAddress(area.process().getAF()),
        .metricIsE2 = configs.get<Config::OspfArea::NSSA_DEFAULT_METRIC_TYPE>().load()
    };

    nssaDefaultRoute = originateExternal(ctx, !add);
}

void OspfOriginator::addDefaultRoute(bool add)
{
    if (area.type != AreaType::NORMAL)
        return;
    if (defaultRoute.has_value() == add)
        return;

    auto configs = area.process().getConfigs();
    AddressFamily af = area.process().getAF();

    bool always = configs.get<Config::Ospf::DEFAULT_ORIGINATE_ALWAYS>().load();

    if (!always)
    {
        auto& rib = area.process().routingInstance->routingTable;
        if (af == AddressFamily::IPv4)
        {
            if (!rib.lookup<uint32_t>(0)) return;
        }
        else
        {
            if (!rib.lookup<__uint128_t>(0)) return;
        }
    }

    ExternalOriginateContext ctx = {
        .lsId = defaultRoute.has_value() ? std::optional{defaultRoute.value().linkStateId} : std::nullopt,
        .prefix = IPPrefix(area.process().getAF()),
        .metric = configs.get<Config::Ospf::DEFAULT_ORIGINATE_METRIC>().load(),
        .tag = 0,
        .nextHop = IPAddress(af),
        .metricIsE2 = configs.get<Config::Ospf::DEFAULT_ORIGINATE_METRIC_TYPE>().load(),
    };

    defaultRoute = originateExternal(ctx, !add);
}

template void OspfOriginator::startRefresh<PolicyV2>(RefreshInfo&);
template void OspfOriginator::startRefresh<PolicyV3>(RefreshInfo&);

template void OspfOriginator::handleRefreshTimeout<PolicyV2>(uint32_t);
template void OspfOriginator::handleRefreshTimeout<PolicyV3>(uint32_t);

template void OspfOriginator::processReoriginatedLsa<PolicyV2>(const LsaKey&, LsaBody&, bool, bool);
template void OspfOriginator::processReoriginatedLsa<PolicyV3>(const LsaKey&, LsaBody&, bool, bool);

template void OspfOriginator::processOriginatedLsa<PolicyV2>(const LsaKey&, LsaBody&, bool, RefreshInfo*);
template void OspfOriginator::processOriginatedLsa<PolicyV3>(const LsaKey&, LsaBody&, bool, RefreshInfo*);
}
