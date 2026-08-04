// ExternalOriginator.cpp

#include "ExternalOriginator.h"
#include "ospf/OspfTypes.hpp"
#include "ospf/OspfProcess.h"
#include "core/VirtualRouter.h"

namespace routing::ospf
{
ExternalOriginator::ExternalOriginator(OspfProcess& proc)
    : process(proc)
{}

template <typename Policy>
void ExternalOriginator::distributeExternalLsa(const OriginatorContext& areaCtx, IncomingLsaContext& lsaCtx, const LsaBody& body)
{
    bool expire = lsaCtx.header.age == OSPF_MAX_AGE;
    {
        process.forEachOriginCtx([&](uint32_t targetAreaId, OriginatorContext& trgCtx) {
            if (targetAreaId == areaCtx.area.areaId)
                return;

            config::ospf::AreaType srcType = areaCtx.area.getType();
            config::ospf::AreaType trgType = trgCtx.area.getType();

            if (lsaCtx.key.lsaType == Policy::NssaType &&
                (srcType == config::ospf::AreaType::NSSA ||
                 srcType == config::ospf::AreaType::TOTALLY_NSSA) &&
                trgType == config::ospf::AreaType::NORMAL)
            {
                translateNssaToExternal<Policy>(trgCtx, lsaCtx.key, body, expire);
                return;
            }

            if (lsaCtx.key.lsaType == Policy::ExternalType && srcType == config::ospf::AreaType::NORMAL)
            {
                trgCtx.processExternalLsa<Policy>(lsaCtx, body);
            }
        });
    }

    std::pair<types::IPPrefix, std::optional<OspfPath>> result;
    {
        auto existingIt = externalDb.find(lsaCtx.key);
        std::optional<uint32_t> seq{std::nullopt};

        if (existingIt != externalDb.end())
            seq = existingIt->second.first.sequence;
            
        if (seq.has_value() && seq.value() >= lsaCtx.header.sequence)
            return;

        auto& rec = externalDb[lsaCtx.key];
        rec.first = lsaCtx.header;
        rec.second = body;

        result = process.externalRouteManager.deriveExternalRoute<Policy>(lsaCtx.key, rec);
    }

    process.rib.replaceExternal(result);

    std::unordered_map<types::IPPrefix, OspfSummaryAddress> activeSummaries;
    {
        if (summaries.empty()) return;
        activeSummaries = summaries;
    }

    syncSummarySuppression<Policy>(activeSummaries);
}

template <typename Policy>
void ExternalOriginator::originateExternal(ExternalOriginateContext& ctx, bool expire)
{
    if (expire) ctx.metric = 0x00FFFFFF;
    auto key = buildExternalKey<Policy>(ctx, false);
    LsaBody& lsa = externalDb[key].second;
    if (!std::holds_alternative<typename Policy::ExternalLsa>(lsa))
        lsa = typename Policy::ExternalLsa{};
    buildExternalBody<Policy>(ctx, std::get<typename Policy::ExternalLsa>(lsa), false);

    process.forEachOriginCtx([&ctx, expire, &lsa, &key](uint32_t, OriginatorContext& areaCtx) {
        if (areaCtx.area.getType() != config::ospf::AreaType::NORMAL)
            return;

        if (ctx.nextHop.has_value())
        {
            auto& external = std::get<typename Policy::ExternalLsa>(lsa);
            bool faValid = areaCtx.isValidForwardAddress(ctx.nextHop.value());
            if constexpr (std::is_same_v<Policy, PolicyV2>)
                external.forwardingAddress = faValid ? ctx.nextHop->v4() : uint32_t{0};
            else
                external.forwardingAddress = faValid ? std::optional<types::IPv6Address>{types::IPv6Address(ctx.nextHop->v6())} : std::nullopt;
        }
        areaCtx.originateLsa<Policy>(key, lsa, expire);
    });
}

template <typename Policy>
void ExternalOriginator::originateExternals(std::vector<std::pair<ExternalOriginateContext, bool>>& ctxs)
{
    for (auto& [ctx, expire]: ctxs)
        if (expire) ctx.metric = 0x00FFFFFF;

    for (auto [ctx, expire] : ctxs)
    {
        LsaKey key = buildExternalKey<Policy>(ctx, false);
        LsaBody& body = externalDb[key].second;
        if (!std::holds_alternative<typename Policy::ExternalLsa>(body))
            body = typename Policy::ExternalLsa{};
        auto& external = std::get<typename Policy::ExternalLsa>(body);
        buildExternalBody<Policy>(ctx, external, false);

        process.forEachOriginCtx([&ctx, &key, &body, &external, expire](uint32_t, OriginatorContext& trgCtx) {
            if (trgCtx.area.getType() != config::ospf::AreaType::NORMAL)
                return;

            if (ctx.nextHop.has_value())
            {
                bool faValid = trgCtx.isValidForwardAddress(ctx.nextHop.value());
                if constexpr (std::is_same_v<Policy, PolicyV2>)
                    external.forwardingAddress = faValid ? ctx.nextHop->v4() : 0;
                else
                    external.forwardingAddress = faValid ? std::optional<types::IPv6Address>{types::IPv6Address(ctx.nextHop->v6())} : std::nullopt;
            }

            trgCtx.originateLsa<Policy>(key, body, expire);
        });
    }
}

template <typename Policy>
LsaKey ExternalOriginator::buildExternalKey(ExternalOriginateContext& ctx, bool isNssa)
{
    LsaKey key;

    key.lsaType = isNssa
        ? Policy::NssaType : Policy::ExternalType;
    key.advertisingRouter = process.getRouterId();
    key.linkStateId = ctx.lsId;

    return key;
}

template <typename Policy>
void ExternalOriginator::buildExternalBody(ExternalOriginateContext& ctx, Policy::ExternalLsa& external, bool isNssa)
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

void ExternalOriginator::addDefaultRoute(bool add)
{
    bool always = process.configs.get<config::Ospf::DEFAULT_ORIGINATE_ALWAYS>().load();

    if (!always)
    {
        auto& globalRib = process.routingInstance->getRib();
        utils::RCU::Guard g;
        if (process.af == types::AddressFamily::IPv4)
        {
            if (!globalRib.lookup<uint32_t>(0, g)) return;
        }
        else
        {
            if (!globalRib.lookup<__uint128_t>(0, g)) return;
        }
    }

    if (!defaultRoute.has_value())
        defaultRoute = process.externalOriginator.monotonicExternalId.fetch_add(1, std::memory_order_release);

    ExternalOriginateContext ctx = {
        .lsId = defaultRoute.value(),
        .prefix = types::IPPrefix(process.af),
        .metric = process.configs.get<config::Ospf::DEFAULT_ORIGINATE_METRIC>().load(),
        .tag = 0,
        .nextHop = std::nullopt,
        .metricIsE2 = process.configs.get<config::Ospf::DEFAULT_ORIGINATE_METRIC_TYPE>().load()
    };

    process.isV3 ? process.externalOriginator.originateExternal<PolicyV3>(ctx, !add)
                 : process.externalOriginator.originateExternal<PolicyV2>(ctx, !add);

    if (!add) defaultRoute.reset();
}

void ExternalOriginator::syncSummaryConfig()
{
    auto cfg = process.configs.get<config::Ospf::SUMMARY_ADDRESS>();

    std::unordered_map<types::IPPrefix, OspfSummaryAddress> active = summaries;

    std::unordered_set<types::IPPrefix> seen;

    cfg.withRead([&](const auto& tsList) {
        for (const auto& ts : tsList)
        {
            const auto& [pfx, noAdv, nssaOnly, tag] = ts;
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
        {
            summaries.erase(it->first);
            it = active.erase(it);
        }
        else
            ++it;
    }

    process.isV3 ? syncSummarySuppression<PolicyV3>(active)
                 : syncSummarySuppression<PolicyV2>(active);
}

template <typename Policy>
void ExternalOriginator::translateNssaToExternal(OriginatorContext& ctx, const LsaKey& key7, const LsaBody& body7, bool expire)
{
    constexpr bool isExtV3 = std::is_same_v<Policy, PolicyV3>;

    const auto& areaCfgs = ctx.getConfigs();

    if constexpr (isExtV3)
    {
        if (std::get<ExternalLsaV3>(body7).prefix.prefixLength == 0 && std::get<ExternalLsaV3>(body7).prefix.addr == 0 &&
            !areaCfgs.get<config::OspfArea::NSSA_DEFAULT_ONLY>().load())
            return;
    }
    else
    {
        if (key7.linkStateId == 0 && std::get<ExternalLsaV2>(body7).networkMask == 0 &&
            !areaCfgs.get<config::OspfArea::NSSA_DEFAULT_ONLY>().load())
            return;
    }

    if (process.configs.get<config::Ospf::LRC_NSSA_TRANSLATION>().load())
    {
        auto& ext7 = std::get<typename Policy::ExternalLsa>(body7);
        auto lookupAddr = [&]() {
            if constexpr (isExtV3)
                return ext7.forwardingAddress && ext7.forwardingAddress->addr
                    ? ext7.forwardingAddress->addr : ext7.prefix.addr;
            else
                return ext7.forwardingAddress
                    ? ext7.forwardingAddress : key7.linkStateId;
        }();

        utils::RCU::Guard g;
        if (!process.routingInstance->getRib().lookup(lookupAddr, g))
            return;
    }

    LsaKey key5;

    key5.lsaType = Policy::ExternalType;
    key5.linkStateId = key7.linkStateId;
    key5.advertisingRouter = process.getRouterId();

    auto& info = ctx.originationState[key5];
    auto& body5 = info.body;
    body5 = body7;

    auto& ext5 = std::get<typename Policy::ExternalLsa>(body5);

    if constexpr (isExtV3)
    {
        ext5.options &= ~0x08;
        if (ext5.forwardingAddress && !ctx.isValidForwardAddress(ext5.forwardingAddress.value()))
            ext5.forwardingAddress.reset();
    }
    else
    {
        if (ext5.forwardingAddress != 0 && !ctx.isValidForwardAddress(ext5.forwardingAddress))
            ext5.forwardingAddress = 0;
    }

    info.expire = expire;

    ctx.processOriginatedLsa<Policy>(key5);
}

template <typename Policy>
void ExternalOriginator::syncSummarySuppression(std::unordered_map<types::IPPrefix, OspfSummaryAddress>& activeSummaries)
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

    specifics.reserve(process.externalOriginator.externalDb.size());

    for (const auto& [k, r] : process.externalOriginator.externalDb)
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

    if (process.configs.get<config::Ospf::DISCARD_EXTERNAL>().load())
    {
        const uint8_t ad = process.configs.get<config::Ospf::DISCARD_EXTERNAL_DISTANCE>().load();

        for (const auto& [sumPfx, s] : activeSummaries)
        {
            const bool valid =
                s.contributorCount > 0 &&
                s.contributorCount != std::numeric_limits<uint32_t>::max() &&
                !s.notAdvertise &&
                s.discardPresent;

            if (valid)
            {
                process.rib.installDiscardRoute(
                    { sumPfx, std::nullopt },
                    s.computedMetric,
                    ad
                );
            }
            else
            {
                process.rib.withdrawDiscardRoute({ sumPfx, std::nullopt });
            }
        }
    }

    process.externalOriginator.originateExternals<Policy>(actions);
}

template void ExternalOriginator::distributeExternalLsa<PolicyV2>(const OriginatorContext&, IncomingLsaContext&, const LsaBody&);
template void ExternalOriginator::distributeExternalLsa<PolicyV3>(const OriginatorContext&, IncomingLsaContext&, const LsaBody&);

template void ExternalOriginator::originateExternal<PolicyV2>(ExternalOriginateContext&, bool);
template void ExternalOriginator::originateExternal<PolicyV3>(ExternalOriginateContext&, bool);

template void ExternalOriginator::originateExternals<PolicyV2>(std::vector<std::pair<ExternalOriginateContext, bool>>&);
template void ExternalOriginator::originateExternals<PolicyV3>(std::vector<std::pair<ExternalOriginateContext, bool>>&);

template LsaKey ExternalOriginator::buildExternalKey<PolicyV2>(ExternalOriginateContext&, bool);
template LsaKey ExternalOriginator::buildExternalKey<PolicyV3>(ExternalOriginateContext&, bool);

template void ExternalOriginator::buildExternalBody<PolicyV2>(ExternalOriginateContext&, PolicyV2::ExternalLsa&, bool);
template void ExternalOriginator::buildExternalBody<PolicyV3>(ExternalOriginateContext&, PolicyV3::ExternalLsa&, bool);

template void ExternalOriginator::translateNssaToExternal<PolicyV2>(OriginatorContext&, const LsaKey&, const LsaBody&, bool);
template void ExternalOriginator::translateNssaToExternal<PolicyV3>(OriginatorContext&, const LsaKey&, const LsaBody&, bool);

template void ExternalOriginator::syncSummarySuppression<PolicyV2>(std::unordered_map<types::IPPrefix, OspfSummaryAddress>&);
template void ExternalOriginator::syncSummarySuppression<PolicyV3>(std::unordered_map<types::IPPrefix, OspfSummaryAddress>&);
}
