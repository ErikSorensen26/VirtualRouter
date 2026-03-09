// OspfProcess.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include <ControlScheduler.h>

#include "OspfProcess.h"
#include "area/Area.h"
#include "topology/RouteManager.h"
#include "OspfTypes.hpp"

namespace OSPF
{
OspfProcess::OspfProcess(bool isV3, uint16_t procId, AddressFamily af, VirtualRouter* vrf)
    : isV3(isV3), routingInstance(vrf), rib(*this), scheduler(vrf->getControlScheduler().create()), procId(procId), af(af), ifaceMgr(*this),
    configs([this, isV3]() {
        auto& registry = routingInstance->getGlobal().registry;
        auto key = Config::generateOspfKey(routingInstance->getInstanceId(), getProcId(), getAF(), isV3);
        if (isV3)
        {
            if (routingInstance->isDefault())
            {
                // TODO: add address family v3 configs from elsewhere
                auto& afCfgs = std::get<V3AfConfigs>(afConfigs);
                return registry.ensure(afCfgs->get<Config::OspfAddressFamilyV3::BASE>(), key);
            }
            // OSPFv3 VRF mode does not support address families
            return registry.create<Config::OspfRegistry>(key);
        }
        else
        {
            // OSPFv2 AddressFamily
            afConfigs.emplace<V2AfConfigs>(registry.create<Config::OspfAddressFamilyV2Registry>(key));
            auto& afCfgs = std::get<V2AfConfigs>(afConfigs);
            auto& v2Base = afCfgs->get<Config::OspfAddressFamilyV2::BASE>();
            return registry.ensure(v2Base, key);
        }
    }())
{
    configs->context().set(this);
    calculateRID();
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
        areas.try_emplace(areaId, *this, areaId); // TODO: maybe add pmr
        setABR(areas.size() > 1 && areas.contains(0));
    }
    return areas.at(areaId);
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
        std::unordered_set<IPPrefix> ranges = val ? area.getRanges() : std::unordered_set<IPPrefix>{};
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
    // TODO: completely reset ospf process
}

void OspfProcess::addDefaultRoute(bool add)
{
    bool always = configs->get<Config::Ospf::DEFAULT_ORIGINATE_ALWAYS>().load();

    if (!always)
    {
        auto& globalRib = routingInstance->getRib();
        if (af == AddressFamily::IPv4)
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
        .prefix = IPPrefix(af),
        .metric = configs->get<Config::Ospf::DEFAULT_ORIGINATE_METRIC>().load(),
        .tag = 0,
        .nextHop = std::nullopt,
        .metricIsE2 = configs->get<Config::Ospf::DEFAULT_ORIGINATE_METRIC_TYPE>().load()
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
                (sourceArea.type == AreaType::NSSA ||
                 sourceArea.type == AreaType::TOTALLY_NSSA) &&
                targetArea.type == AreaType::NORMAL)
            {
                targetArea.getOriginator().translateNssaToExternal(ctx.key, body, expire);
                continue;
            }

            if (ctx.key.lsaType == Policy::ExternalType && sourceArea.type == AreaType::NORMAL)
            {
                targetArea.processExternalLsa<Policy>(ctx, body);
            }
        }
    }

    std::pair<IPPrefix, std::optional<OspfPath>> result;
    {
        auto existingIt = externalDb.find(ctx.key);
        std::optional<uint32_t> seq{std::nullopt};

        if (existingIt != externalDb.end())
            seq = existingIt->second.first.sequence;
            
        if (!seq.has_value() || seq.value() < ctx.header.sequence)
            return;

        auto& rec = externalDb[ctx.key];
        rec.first = ctx.header;
        rec.second = body;

        result = RouteManager::deriveExternalRoute<Policy>(*this, ctx.key, rec);
    }

    rib.replaceExternal(result);

    std::unordered_map<IPPrefix, OspfSummaryAddress> activeSummaries;
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
        external.prefix = ctx.prefix;
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
        if (area.type != AreaType::NORMAL)
            continue;

        if (ctx.nextHop.has_value())
        {
            auto& external = std::get<typename Policy::ExternalLsa>(lsa);
            bool faValid = area.isValidForwardAddress(ctx.nextHop.value());
            if constexpr (std::is_same_v<Policy, PolicyV2>)
                external.forwardingAddress = faValid ? readU32(ctx.nextHop->raw) : 0;
            else
                external.forwardingAddress = faValid ? std::optional{ctx.nextHop.value()} : std::nullopt;
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
            if (area.type != AreaType::NORMAL)
                continue;

            if (ctx.nextHop.has_value())
            {
                bool faValid = area.isValidForwardAddress(ctx.nextHop.value());
                if constexpr (std::is_same_v<Policy, PolicyV2>)
                    external.forwardingAddress = faValid ? readU32(ctx.nextHop->raw) : 0;
                else
                    external.forwardingAddress = faValid ? std::optional{ctx.nextHop.value()} : std::nullopt;
            }

            area.getOriginator().originateLsa<Policy>(key, body, expire);
        }
    }
}

void OspfProcess::syncSummaryConfig()
{
    auto& cfg = configs->get<Config::Ospf::SUMMARY_ADDRESS>();

    std::unordered_map<IPPrefix, OspfSummaryAddress> active = summaries;

    std::unordered_set<IPPrefix> seen;

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
void OspfProcess::syncSummarySuppression(std::unordered_map<IPPrefix, OspfSummaryAddress>& activeSummaries)
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
            return ext.prefix;
        else
            return IPPrefix(k.linkStateId, static_cast<uint8_t>(std::popcount(ext.networkMask)));
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
        IPPrefix prefix;
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

        const IPPrefix pfx = entryPrefix(k, body);

        bool coveredByValidSummary = false;

        for (auto& [sumPfx, s] : activeSummaries)
        {
            if (!Functions::compareNetworkWithIp(sumPfx.addr, pfx.addr, sumPfx.prefixLength, pfx.af))
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

    if (configs->get<Config::Ospf::DISCARD_EXTERNAL>().load())
    {
        const uint8_t ad = configs->get<Config::Ospf::DISCARD_EXTERNAL_DISTANCE>().load();

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
            network.networkMask = Functions::prefixTo32Mask(path.prefix.prefixLength);
            network.metric = static_cast<uint32_t>(path.cost);

            key.advertisingRouter = getRouterId();
            key.linkStateId = readU32(path.prefix.addr);
            key.lsaType = OSPFV2_LSA_SUM_NET;
        }
        else
        {
            network.prefix = path.prefix;
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
        network.networkMask = Functions::prefixTo32Mask(path.prefix.prefixLength);
        network.metric = static_cast<uint32_t>(path.cost);

        key.advertisingRouter = getRouterId();
        key.linkStateId = readU32(path.prefix.addr);
        key.lsaType = OSPFV2_LSA_SUM_NET;
    }
    else
    {
        network.prefix = path.prefix;
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

template void OspfProcess::syncSummarySuppression<PolicyV2>(std::unordered_map<IPPrefix, OspfSummaryAddress>&);
template void OspfProcess::syncSummarySuppression<PolicyV3>(std::unordered_map<IPPrefix, OspfSummaryAddress>&);

template void OspfProcess::reoriginateSummaries<PolicyV2>(Area&, std::vector<OspfRouteChange>&);
template void OspfProcess::reoriginateSummaries<PolicyV3>(Area&, std::vector<OspfRouteChange>&);

template void OspfProcess::reoriginateSummary<PolicyV2>(Area&, OspfRouteChange&);
template void OspfProcess::reoriginateSummary<PolicyV3>(Area&, OspfRouteChange&);
}
