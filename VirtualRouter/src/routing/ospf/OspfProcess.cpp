// OspfProcess.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include <ControlScheduler.h>

#include "OspfProcess.h"
#include "area/Area.h"
#include "topology/RouteManager.h"
#include "OspfTypes.hpp"

namespace routing::ospf
{
OspfProcess::OspfProcess(bool isV3, uint16_t procId, types::AddressFamily af, core::VirtualRouter* vrf)
    : isV3(isV3), routingInstance(vrf), rib(*this), scheduler(vrf->getControlScheduler().create()), procId(procId), af(af), ifaceMgr(*this),
    configs([this, isV3]() {
        auto& registry = routingInstance->getGlobal().registry;
        if (isV3)
        {
            if (routingInstance->isDefault())
            {
                // TODO: add address family v3 configs from elsewhere
                auto& afCfgs = std::get<V3AfConfigs>(afConfigs);
                return registry.emplace(afCfgs.get<config::OspfAddressFamilyV3::BASE>());
            }
            // OSPFv3 VRF mode does not support address families
            return registry.create<config::OspfRegistry>();
        }
        else
        {
            // OSPFv2 types::AddressFamily
            afConfigs.emplace<V2AfConfigs>(registry.create<config::OspfAddressFamilyV2Registry>());
            auto& afCfgs = std::get<V2AfConfigs>(afConfigs);
            auto& v2Base = afCfgs.get<config::OspfAddressFamilyV2::BASE>();
            return registry.emplace(v2Base);
        }
    }())
{
    configs->context().set(this);
    calculateRID();

    // Subscribe to interface lifecycle events so the interface list stays
    auto& ifMgr = vrf->getInterfaceManager();

    auto postRefresh = [](void* ctx, interface::Interface&) {
        auto* p = static_cast<OspfProcess*>(ctx);
        p->scheduler.post([p]{ p->ifaceMgr.refreshInterfaceList(); });
    };

    ifUpId   = ifMgr.subscribe(interface::StateChange::IF_READY, this, postRefresh);
    ifDownId = ifMgr.subscribe(interface::StateChange::IF_DOWN,  this, postRefresh);

    if (!isV3)
    {
        auto postRefreshV4 = [](void* ctx, interface::Interface&, types::IPv4Prefix&) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p]{ p->ifaceMgr.refreshInterfaceList(); });
        };
        ipReadyId = ifMgr.subscribe(interface::IPv4Event::IPV4_READY, this, postRefreshV4);
        ipDelId   = ifMgr.subscribe(interface::IPv4Event::IPV4_DEL,   this, postRefreshV4);
    }
    else
    {
        auto postRefreshV6 = [](void* ctx, interface::Interface&, types::IPv6Prefix&) {
            auto* p = static_cast<OspfProcess*>(ctx);
            p->scheduler.post([p]{ p->ifaceMgr.refreshInterfaceList(); });
        };
        ipReadyId = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_READY, this, postRefreshV6);
        ipDelId   = ifMgr.subscribe(interface::IPv6Event::IPV6_LL_DEL,   this, postRefreshV6);
    }
}

OspfProcess::~OspfProcess()
{
    // Unsubscribe before the scheduler and interface state tear down.
    auto& ifMgr = routingInstance->getInterfaceManager();
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{ifUpId});
    ifMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{ifDownId});
    if (!isV3)
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{ipDelId});
    }
    else
    {
        ifMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{ipReadyId});
        ifMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{ipDelId});
    }
    ifaceMgr.deactivateAll();
}

bool OspfProcess::calculateRID()
{
    return routingInstance->calculateRID(rid);
}

Area* OspfProcess::getArea(uint32_t areaId)
{
    auto it = areas.find(areaId);
    if (it == areas.end()) return nullptr;
    return &it->second;
}
    
Area& OspfProcess::insureArea(uint32_t areaId)
{
    if (!areas.contains(areaId))
    {
        areas.try_emplace(areaId, *this, areaId);
        setABR(areas.size() > 1 && areas.contains(0));
    }
    return areas.at(areaId);
}

void OspfProcess::removeArea(uint32_t areaId)
{
    if (!areas.contains(areaId)) return;
    areas.erase(areaId);
    setABR(areas.size() > 1 && areas.contains(0));
}

void OspfProcess::setASBR(bool val)
{
    bool current = asbr;
    if (current == val) return;

    // Refresh default routes
    for (auto& [id, area] : areas)
    {
        area.getOriginator().fullRefresh();
    }
}

void OspfProcess::setABR(bool val)
{
    bool current = abr;
    if (current == val) return;

    // Refresh ranges
    for (auto& [id, area] : areas)
    {
        // Ranges only need to be specified when they are in use.
        std::unordered_set<types::IPPrefix> ranges = val ? area.getRanges() : std::unordered_set<types::IPPrefix>{};
        area.syncRangeSuppression(area.getRanges(), true);
    }
}

bool OspfProcess::isASBR()
{
    return asbr;
}

bool OspfProcess::isABR()
{
    return abr;
}

void OspfProcess::initiateReset()
{
    scheduler.post([this] {
        for (auto& [id, area] : areas)
            area.reset();
    });
}

void OspfProcess::addDefaultRoute(bool add)
{
    bool always = configs.get<config::Ospf::DEFAULT_ORIGINATE_ALWAYS>().load();

    if (!always)
    {
        auto& globalRib = routingInstance->getRib();
        if (af == types::AddressFamily::IPv4)
        {
            if (!globalRib.lookup<uint32_t>(0)) return;
        }
        else
        {
            if (!globalRib.lookup<__uint128_t>(0)) return;
        }
    }

    if (!defaultRoute.has_value())
        defaultRoute = monotonicExternalId.fetch_add(1, std::memory_order_release);

    ExternalOriginateContext ctx = {
        .lsId = defaultRoute.value(),
        .prefix = types::IPPrefix(af),
        .metric = configs.get<config::Ospf::DEFAULT_ORIGINATE_METRIC>().load(),
        .tag = 0,
        .nextHop = std::nullopt,
        .metricIsE2 = configs.get<config::Ospf::DEFAULT_ORIGINATE_METRIC_TYPE>().load()
    };

    isV3 ? originateExternal<PolicyV3>(ctx, !add)
         : originateExternal<PolicyV2>(ctx, !add);

    if (!add) defaultRoute.reset();
}

template <typename Policy>
void OspfProcess::distributeExternalLsa(const Area& sourceArea, IncomingLsaContext& ctx, const LsaBody& body)
{
    bool expire = ctx.header.age == OSPF_MAX_AGE;
    {
        for (auto& [targetAreaId, targetArea] : areas)
        {
            if (targetAreaId == sourceArea.areaId)
                continue;

            if (ctx.key.lsaType == Policy::NssaType &&
                (sourceArea.type == config::ospf::AreaType::NSSA ||
                 sourceArea.type == config::ospf::AreaType::TOTALLY_NSSA) &&
                targetArea.type == config::ospf::AreaType::NORMAL)
            {
                targetArea.getOriginator().translateNssaToExternal(ctx.key, body, expire);
                continue;
            }

            if (ctx.key.lsaType == Policy::ExternalType && sourceArea.type == config::ospf::AreaType::NORMAL)
            {
                targetArea.processExternalLsa<Policy>(ctx, body);
            }
        }
    }

    std::pair<types::IPPrefix, std::optional<OspfPath>> result;
    {
        auto existingIt = externalDb.find(ctx.key);
        std::optional<uint32_t> seq{std::nullopt};

        if (existingIt != externalDb.end())
            seq = existingIt->second.first.sequence;
            
        if (seq.has_value() && seq.value() >= ctx.header.sequence)
            return;

        auto& rec = externalDb[ctx.key];
        rec.first = ctx.header;
        rec.second = body;

        result = routemanager::deriveExternalRoute<Policy>(*this, ctx.key, rec);
    }

    rib.replaceExternal(result);

    std::unordered_map<types::IPPrefix, OspfSummaryAddress> activeSummaries;
    {
        if (summaries.empty()) return;
        activeSummaries = summaries;
    }

    syncSummarySuppression<Policy>(activeSummaries);
}

template <typename Policy>
LsaKey OspfProcess::buildExternalKey(ExternalOriginateContext& ctx, bool isNssa)
{
    LsaKey key;

    key.lsaType = isNssa
        ? Policy::NssaType : Policy::ExternalType;
    key.advertisingRouter = getRouterId();
    key.linkStateId = ctx.lsId;

    return key;
}

template <typename Policy>
void OspfProcess::buildExternalBody(ExternalOriginateContext& ctx, Policy::ExternalLsa& external, bool isNssa)
{
    constexpr bool isExtV3 = std::is_same_v<Policy, PolicyV3>;

    if constexpr (isExtV3)
    {
        if (isNssa) external.options ^= 0x08;
    }

    external.isType2 = ctx.metricIsE2;
    external.metric = ctx.metric;

    if constexpr (isExtV3)
    {
        external.prefix = types::IPv6Prefix(ctx.prefix.v6(), ctx.prefix.prefixLength, true);
        external.referencedLsType = 0;
        if (ctx.tag != 0)
            external.routeTag = ctx.tag;
    }
    else
    {
        external.networkMask = ctx.prefix.getMask();
        external.isType2 = ctx.metricIsE2;
        external.routeTag = ctx.tag;
    }
}

template <typename Policy>
void OspfProcess::originateExternal(ExternalOriginateContext& ctx, bool expire)
{
    if (expire) ctx.metric = 0x00FFFFFF;
    auto key = buildExternalKey<Policy>(ctx, false);
    LsaBody& lsa = externalDb[key].second;
    buildExternalBody<Policy>(ctx, std::get<typename Policy::ExternalLsa>(lsa), false);

    for (auto& [id, area] : areas)
    {
        if (area.type != config::ospf::AreaType::NORMAL)
            continue;

        if (ctx.nextHop.has_value())
        {
            auto& external = std::get<typename Policy::ExternalLsa>(lsa);
            bool faValid = area.isValidForwardAddress(ctx.nextHop.value());
            if constexpr (std::is_same_v<Policy, PolicyV2>)
                external.forwardingAddress = faValid ? ctx.nextHop->v4() : uint32_t{0};
            else
                external.forwardingAddress = faValid ? std::optional<types::IPv6Address>{types::IPv6Address(ctx.nextHop->v6())} : std::nullopt;
        }
        area.getOriginator().originateLsa<Policy>(key, lsa, expire);
    }
}

template <typename Policy>
void OspfProcess::originateExternals(std::vector<std::pair<ExternalOriginateContext, bool>>& ctxs)
{
    for (auto& [ctx, expire]: ctxs)
        if (expire) ctx.metric = 0x00FFFFFF;

    for (auto [ctx, expire] : ctxs)
    {
        LsaKey key = buildExternalKey<Policy>(ctx, false);
        LsaBody& body = externalDb[key].second;
        auto& external = std::get<typename Policy::ExternalLsa>(body);
        buildExternalBody<Policy>(ctx, external, false);

        for (auto& [id, area] : areas)
        {
            if (area.type != config::ospf::AreaType::NORMAL)
                continue;

            if (ctx.nextHop.has_value())
            {
                bool faValid = area.isValidForwardAddress(ctx.nextHop.value());
                if constexpr (std::is_same_v<Policy, PolicyV2>)
                    external.forwardingAddress = faValid ? ctx.nextHop->v4() : 0;
                else
                    external.forwardingAddress = faValid ? std::optional<types::IPv6Address>{types::IPv6Address(ctx.nextHop->v6())} : std::nullopt;
            }

            area.getOriginator().originateLsa<Policy>(key, body, expire);
        }
    }
}

void OspfProcess::syncSummaryConfig()
{
    auto& cfg = configs.get<config::Ospf::SUMMARY_ADDRESS>();

    std::unordered_map<types::IPPrefix, OspfSummaryAddress> active = summaries;

    std::unordered_set<types::IPPrefix> seen;

    cfg.withRead([&](const auto& ts) {
        for (const auto& [pfx, noAdv, nssaOnly, tag] : ts)
        {
            seen.insert(pfx);

            auto& s = active[pfx];
            s.notAdvertise = noAdv;
            s.nssaOnly = nssaOnly;
            s.tag = tag;
        }
    });

    for (auto it = active.begin(); it != active.end();)
    {
        if (seen.find(it->first) == seen.end())
            it = summaries.erase(it);
        else
            ++it;
    }

    isV3 ? syncSummarySuppression<PolicyV2>(active)
         : syncSummarySuppression<PolicyV3>(active);
}

template <typename Policy>
void OspfProcess::syncSummarySuppression(std::unordered_map<types::IPPrefix, OspfSummaryAddress>& activeSummaries)
{
    for (auto& [_, s] : activeSummaries)
    {
        s.contributorCount = 0;
        s.computedMetric = std::numeric_limits<uint32_t>::max();
        s.isType2 = false;
        s.discardPresent = false;
    }

    auto entryPrefix = [&](const LsaKey& k, const LsaBody& body)
    {
        const auto& ext = std::get<typename Policy::ExternalLsa>(body);
        if constexpr (std::is_same_v<Policy, PolicyV3>)
            return types::IPPrefix(ext.prefix.addr, ext.prefix.prefixLength);
        else
            return types::IPPrefix(k.linkStateId, static_cast<uint8_t>(std::popcount(ext.networkMask)));
    };

    auto metricOf = [&](const LsaBody& b)
    {
        return std::get<typename Policy::ExternalLsa>(b).metric;
    };

    auto isE2Of = [&](const LsaBody& b)
    {
        return std::get<typename Policy::ExternalLsa>(b).isType2;
    };

    auto tagOf = [&](const LsaBody& b)
    {
        const auto& ext = std::get<typename Policy::ExternalLsa>(b);
        if constexpr (std::is_same_v<Policy, PolicyV2>)
            return ext.routeTag;
        else
            return ext.routeTag.value_or(0);
    };

    struct SpecificState
    {
        uint32_t lsId;
        types::IPPrefix prefix;
        uint32_t originalMetric;
        uint32_t originalTag;
        bool originalIsE2;
        bool covered = false;
        bool suppressed = false;
    };

    std::vector<SpecificState> specifics;
    std::vector<std::pair<ExternalOriginateContext, bool>> actions;

    specifics.reserve(externalDb.size());

    for (const auto& [k, r] : externalDb)
    {
        const LsaHeader& hdr = r.first;
        const LsaBody& body = r.second;

        const uint32_t metric = metricOf(body);
        const bool isE2 = isE2Of(body);

        if (hdr.age == OSPF_MAX_AGE)
        {
            specifics.push_back({
                .lsId = k.linkStateId,
                .prefix = entryPrefix(k, body),
                .originalMetric = metric,
                .originalTag = tagOf(body),
                .originalIsE2 = isE2,
                .covered = false,
                .suppressed = true
            });
            continue;
        }

        const types::IPPrefix pfx = entryPrefix(k, body);

        bool coveredByValidSummary = false;

        for (auto& [sumPfx, s] : activeSummaries)
        {
            if (!sumPfx.contains(types::IPAddress(pfx.addr, pfx.prefixLength)))
                continue;

            if (s.contributorCount == 0)
            {
                s.isType2 = isE2;
            }
            if (s.contributorCount != 0 && s.isType2 != isE2)
            {
                s.contributorCount = std::numeric_limits<uint32_t>::max();
                continue;
            }

            if (s.contributorCount != std::numeric_limits<uint32_t>::max())
                ++s.contributorCount;

            s.computedMetric = std::min(s.computedMetric, metric);
            coveredByValidSummary = true;
        }

        specifics.push_back({
            .lsId = k.linkStateId,
            .prefix = pfx,
            .originalMetric = metricOf(body),
            .originalTag = tagOf(body),
            .originalIsE2 = isE2Of(body),
            .covered = coveredByValidSummary,
            .suppressed = false
        });
    }

    for (auto& [_, s] : activeSummaries)
    {
        const bool valid =
            s.contributorCount > 0 &&
            s.contributorCount != std::numeric_limits<uint32_t>::max() &&
            !s.notAdvertise;

        if (!valid)
            s.computedMetric = 0;
    }

    for (const auto& sp : specifics)
    {
        const auto sit = activeSummaries.find(sp.prefix);
        const bool summaryValid =
            sit != activeSummaries.end() &&
            sit->second.contributorCount > 0 &&
            sit->second.contributorCount != std::numeric_limits<uint32_t>::max() &&
            !sit->second.notAdvertise;

        if (sp.covered && summaryValid && !sp.suppressed)
        {
            actions.emplace_back(
                ExternalOriginateContext{
                    .lsId       = sp.lsId,
                    .prefix     = sp.prefix,
                    .metric     = sp.originalMetric,
                    .tag        = sp.originalTag,
                    .nextHop    = std::nullopt,
                    .metricIsE2 = sp.originalIsE2
                }, false);
        }
        else if (!sp.covered && sp.suppressed)
        {
            actions.emplace_back(
                ExternalOriginateContext{
                    .lsId       = sp.lsId,
                    .prefix     = sp.prefix,
                    .metric     = 0x00FFFFFF,
                    .tag        = sp.originalTag,
                    .nextHop    = std::nullopt,
                    .metricIsE2 = sp.originalIsE2
                }, true);
        }
    }

    for (const auto& [sumPfx, s] : activeSummaries)
    {
        const bool valid =
            s.contributorCount > 0 &&
            s.contributorCount != std::numeric_limits<uint32_t>::max() &&
            !s.notAdvertise;

        if (valid && !s.discardPresent)
        {
            actions.emplace_back(
                ExternalOriginateContext{
                    .lsId       = s.lsId,
                    .prefix     = sumPfx,
                    .metric     = s.computedMetric,
                    .tag        = s.tag.value_or(0),
                    .nextHop    = std::nullopt,
                    .metricIsE2 = s.isType2
                }, false);
        }
        else if (!valid && s.discardPresent)
        {
            actions.emplace_back(
                ExternalOriginateContext{
                    .lsId       = s.lsId,
                    .prefix     = sumPfx,
                    .metric     = 0x00FFFFFF,
                    .tag        = s.tag.value_or(0),
                    .nextHop    = std::nullopt,
                    .metricIsE2 = s.isType2
                }, true);
        }
    }

    if (configs.get<config::Ospf::DISCARD_EXTERNAL>().load())
    {
        const uint8_t ad = configs.get<config::Ospf::DISCARD_EXTERNAL_DISTANCE>().load();

        for (const auto& [sumPfx, s] : activeSummaries)
        {
            const bool valid =
                s.contributorCount > 0 &&
                s.contributorCount != std::numeric_limits<uint32_t>::max() &&
                !s.notAdvertise &&
                s.discardPresent;

            if (valid)
            {
                rib.installDiscardRoute(
                    { sumPfx, std::nullopt },
                    s.computedMetric,
                    ad
                );
            }
            else
            {
                rib.withdrawDiscardRoute({ sumPfx, std::nullopt });
            }
        }
    }

    originateExternals<Policy>(actions);
}

template <typename Policy>
void OspfProcess::reoriginateSummaries(Area& sourceArea, std::vector<OspfRouteChange>& pathList)
{
    if (!isABR() || areas.size() == 1) return;

    std::vector<std::pair<LsaKey, LsaBody>> networks;

    for (const auto& path : pathList)
    {
        auto& net = networks.emplace_back(LsaKey{}, typename Policy::InterNetworkLsa{});

        LsaKey& key = net.first;
        LsaBody& n = net.second;

        typename Policy::InterNetworkLsa& network = std::get<typename Policy::InterNetworkLsa>(n);

        if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>)
        {
            network.networkMask = types::v4Mask(path.prefix.prefixLength);
            network.metric = static_cast<uint32_t>(path.cost);

            key.advertisingRouter = getRouterId();
            key.linkStateId = path.prefix.v4();
            key.lsaType = OSPFV2_LSA_SUM_NET;
        }
        else
        {
            network.prefix = types::IPv6Prefix(path.prefix.v6(), path.prefix.prefixLength, true);
            network.metric = static_cast<uint32_t>(path.cost);
            network.options = path.options;

            key.advertisingRouter = getRouterId();
            if (auto it = intraLsids.find(path.prefix); it != intraLsids.end())
            {
                key.linkStateId = it->second;
            }
            else
            {
                key.linkStateId = monotonicIntraId.fetch_add(1, std::memory_order_release);
                intraLsids.emplace(path.prefix, key.linkStateId);
            }

            key.lsaType = OSPFV3_LSA_INTER_AREA_PREFIX;
        }
    }

    {
        uint32_t sourceAreaId = sourceArea.areaId;

        auto processLsas = [&](Area& a)
        {
            for (auto& [key, network] : networks)
                a.getOriginator().originateLsa<Policy>(key, network, false);
        };

        if (sourceAreaId == 0) // Transit area reoriginates to all other normal areas.
        {
            for (auto& [id, area] : areas)
            {
                if (id == 0) continue;
                if (!area.getFlags().getExternalRouting())
                    continue;
                processLsas(area);
            }
        }
        else if (auto* area = getArea(1); area) // Normal areas reoriginate to Transit area.
        {
            processLsas(*area);
        }
    }
}

template <typename Policy>
void OspfProcess::reoriginateSummary(Area& sourceArea, OspfRouteChange& path)
{
    if (!isABR() || areas.size() == 1) return;

    std::vector<std::pair<LsaKey, LsaBody>> networks;

    LsaKey key;
    LsaBody summary = typename Policy::InterNetworkLsa();

    typename Policy::InterNetworkLsa& network = std::get<typename Policy::InterNetworkLsa>(summary);

    if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>)
    {
        network.networkMask = types::v4Mask(path.prefix.prefixLength);
        network.metric = static_cast<uint32_t>(path.cost);

        key.advertisingRouter = getRouterId();
        key.linkStateId = path.prefix.v4();
        key.lsaType = OSPFV2_LSA_SUM_NET;
    }
    else
    {
        network.prefix = types::IPv6Prefix(path.prefix.v6(), path.prefix.prefixLength, true);
        network.metric = static_cast<uint32_t>(path.cost);
        network.options = path.options;

        key.advertisingRouter = getRouterId();
        if (auto it = intraLsids.find(path.prefix); it != intraLsids.end())
        {
            key.linkStateId = it->second;
        }
        else
        {
            key.linkStateId = monotonicIntraId.fetch_add(1, std::memory_order_release);
            intraLsids.emplace(path.prefix, key.linkStateId);
        }

        key.lsaType = OSPFV3_LSA_INTER_AREA_PREFIX;
    }

    uint32_t sourceAreaId = sourceArea.areaId;

    auto processLsa = [&](Area& a)
    {
        a.getOriginator().originateLsa<Policy>(key, summary, false);
    };

    if (sourceAreaId == 0) // Transit area reoriginates to all other normal areas.
    {
        for (auto& [id, area] : areas)
        {
            if (id == 0) continue;
            if (!area.getFlags().getExternalRouting())
                continue;
            processLsa(area);
        }
    }
    else if (auto* area = getArea(1); area) // Normal areas reoriginate to Transit area.
    {
        processLsa(*area);
    }
}

template void OspfProcess::distributeExternalLsa<PolicyV2>(const Area&, IncomingLsaContext&, const LsaBody&);
template void OspfProcess::distributeExternalLsa<PolicyV3>(const Area&, IncomingLsaContext&, const LsaBody&);

template void OspfProcess::originateExternal<PolicyV2>(ExternalOriginateContext&, bool);
template void OspfProcess::originateExternal<PolicyV3>(ExternalOriginateContext&, bool);

template void OspfProcess::originateExternals<PolicyV2>(std::vector<std::pair<ExternalOriginateContext, bool>>&);
template void OspfProcess::originateExternals<PolicyV3>(std::vector<std::pair<ExternalOriginateContext, bool>>&);

template LsaKey OspfProcess::buildExternalKey<PolicyV2>(ExternalOriginateContext&, bool);
template LsaKey OspfProcess::buildExternalKey<PolicyV3>(ExternalOriginateContext&, bool);

template void OspfProcess::buildExternalBody<PolicyV2>(ExternalOriginateContext&, PolicyV2::ExternalLsa&, bool);
template void OspfProcess::buildExternalBody<PolicyV3>(ExternalOriginateContext&, PolicyV3::ExternalLsa&, bool);

template void OspfProcess::syncSummarySuppression<PolicyV2>(std::unordered_map<types::IPPrefix, OspfSummaryAddress>&);
template void OspfProcess::syncSummarySuppression<PolicyV3>(std::unordered_map<types::IPPrefix, OspfSummaryAddress>&);

template void OspfProcess::reoriginateSummaries<PolicyV2>(Area&, std::vector<OspfRouteChange>&);
template void OspfProcess::reoriginateSummaries<PolicyV3>(Area&, std::vector<OspfRouteChange>&);

template void OspfProcess::reoriginateSummary<PolicyV2>(Area&, OspfRouteChange&);
template void OspfProcess::reoriginateSummary<PolicyV3>(Area&, OspfRouteChange&);
} // namespace routing
