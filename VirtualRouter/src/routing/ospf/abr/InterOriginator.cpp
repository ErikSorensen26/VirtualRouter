// InterOriginator.cpp

#include "InterOriginator.h"
#include "ospf/area/Area.h"
#include "ospf/area/IntraOriginator.h"
#include "ospf/OspfProcess.h"
#include "ospf/asbr/ExternalOriginator.h"
#include "VirtualRouter.h"
#include "ospf/area/OriginatorContext.h"

namespace routing::ospf
{
InterOriginator::InterOriginator(OspfProcess& proc)
    : process(proc)
{}

template <typename Policy>
void InterOriginator::originateSummary(OriginatorContext& ctx, uint32_t lsid, const types::IPPrefix& prefix, uint32_t cost, bool expire)
{
    LsaKey key;

    key.lsaType = Policy::InterNetworkType;
    key.linkStateId = lsid;
    key.advertisingRouter = process.getRouterId();

    auto& info = ctx.originationState[key];
    auto& body = info.body;

    if constexpr (std::is_same_v<Policy, PolicyV3>)
    {
        if (process.af == types::AddressFamily::IPv4)
        {
            body = InterAreaPrefixLsaV4();
            auto& summary = std::get<InterAreaPrefixLsaV4>(body);
            summary.metric = cost;
            summary.options = 0;
            summary.prefix = types::IPv4Prefix(prefix.v4(), prefix.prefixLength, true);
        }
        else
        {
            body = InterAreaPrefixLsa();
            auto& summary = std::get<InterAreaPrefixLsa>(body);
            summary.metric = cost;
            summary.options = 0;
            summary.prefix = types::IPv6Prefix(prefix.v6(), prefix.prefixLength, true);
        }
    }
    else
    {
        body = typename Policy::InterNetworkLsa();
        auto& summary = std::get<typename Policy::InterNetworkLsa>(body);
        summary.metric = cost;
        summary.networkMask = prefix.getMask();
    }

    info.expire = expire;

    ctx.processOriginatedLsa<Policy>(key);
}

template <typename Policy>
void InterOriginator::addAsbrLsa(OriginatorContext& ctx, uint32_t asbr, bool refresh)
{
    LsaKey key(Policy::InterRouterType, asbr, process.getRouterId());

    auto& info = ctx.originationState[key];
    LsaBody& lsa = info.body;
    auto* oldPtr = std::get_if<typename Policy::InterRouterLsa>(&lsa);
    std::optional<typename Policy::InterRouterLsa> oldLsa;
    if (oldPtr) oldLsa = *oldPtr;
    lsa = typename Policy::InterRouterLsa{};
    auto& asbrLsa = std::get<typename Policy::InterRouterLsa>(lsa);

    uint32_t metric = process.table.lookupDistance(asbr);
    if (metric == 0) return;
    asbrLsa.metric = metric;

    if constexpr (std::is_same_v<Policy, PolicyV3>)
    {
        asbrLsa.destinationRouterId = asbr;
        auto it = ctx.asbrLsas.find(asbr);
        if (!refresh && it != ctx.asbrLsas.end() && oldLsa && *oldLsa == asbrLsa && key == it->second)
            return;
    }
    else
    {
        if (!refresh && oldLsa && *oldLsa == asbrLsa)
            return;
    }

    ctx.asbrLsas[asbr] = key;
    ctx.processOriginatedLsa<Policy>(key);
}

template <typename Policy>
void InterOriginator::reoriginateSummaries(OriginatorContext& ctx, std::vector<OspfRouteChange>& pathList)
{
    if (!process.isABR() || process.areaSize() == 1) return;

    std::vector<std::pair<LsaKey, LsaBody>> networks;

    for (const auto& path : pathList)
    {
        LsaKey key;
        LsaBody n;

        if constexpr (std::is_same_v<Policy, PolicyV3>)
        {
            key.advertisingRouter = process.getRouterId();
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

            if (process.af == types::AddressFamily::IPv4)
            {
                n = InterAreaPrefixLsaV4();
                auto& network = std::get<InterAreaPrefixLsaV4>(n);
                network.prefix = types::IPv4Prefix(path.prefix.v4(), path.prefix.prefixLength, true);
                network.metric = static_cast<uint32_t>(path.cost);
                network.options = path.options;
            }
            else
            {
                n = InterAreaPrefixLsa();
                auto& network = std::get<InterAreaPrefixLsa>(n);
                network.prefix = types::IPv6Prefix(path.prefix.v6(), path.prefix.prefixLength, true);
                network.metric = static_cast<uint32_t>(path.cost);
                network.options = path.options;
            }
        }
        else
        {
            n = SummaryNetworkLsa();
            auto& network = std::get<SummaryNetworkLsa>(n);
            network.networkMask = types::v4Mask(path.prefix.prefixLength);
            network.metric = static_cast<uint32_t>(path.cost);

            key.advertisingRouter = process.getRouterId();
            key.linkStateId = path.prefix.v4();
            key.lsaType = OSPFV2_LSA_SUM_NET;
        }

        networks.emplace_back(key, std::move(n));
    }

    {
        uint32_t sourceAreaId = ctx.area.areaId;

        auto processLsas = [&](OriginatorContext& a)
        {
            for (auto& [key, network] : networks)
                a.originateLsa<Policy>(key, network, false);
        };

        if (sourceAreaId == 0) // Transit area reoriginates to all other normal areas.
        {
            process.forEachOriginCtx([&](uint32_t id, OriginatorContext& context) {
                if (id == 0) return;
                if (!AreaFlagManager::getExternalRouting(context.getAreaFlags()))
                    return;
                processLsas(context);
            });
        }
        else if (auto* context = process.getOriginCtx(0); context) // Normal areas reoriginate to Transit area.
        {
            processLsas(*context);
        }
    }
}

template <typename Policy>
void InterOriginator::reoriginateSummary(OriginatorContext& ctx, OspfRouteChange& path)
{
    if (!process.isABR() || process.areaSize() == 1) return;

    std::vector<std::pair<LsaKey, LsaBody>> networks;

    LsaKey key;
    LsaBody summary;

    if constexpr (std::is_same_v<Policy, PolicyV3>)
    {
        key.advertisingRouter = process.getRouterId();
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

        if (process.af == types::AddressFamily::IPv4)
        {
            summary = InterAreaPrefixLsaV4();
            auto& network = std::get<InterAreaPrefixLsaV4>(summary);
            network.prefix = types::IPv4Prefix(path.prefix.v4(), path.prefix.prefixLength, true);
            network.metric = static_cast<uint32_t>(path.cost);
            network.options = path.options;
        }
        else
        {
            summary = InterAreaPrefixLsa();
            auto& network = std::get<InterAreaPrefixLsa>(summary);
            network.prefix = types::IPv6Prefix(path.prefix.v6(), path.prefix.prefixLength, true);
            network.metric = static_cast<uint32_t>(path.cost);
            network.options = path.options;
        }
    }
    else
    {
        summary = SummaryNetworkLsa();
        auto& network = std::get<SummaryNetworkLsa>(summary);
        network.networkMask = types::v4Mask(path.prefix.prefixLength);
        network.metric = static_cast<uint32_t>(path.cost);

        key.advertisingRouter = process.getRouterId();
        key.linkStateId = path.prefix.v4();
        key.lsaType = OSPFV2_LSA_SUM_NET;
    }

    uint32_t sourceAreaId = ctx.area.areaId;

    auto processLsa = [&key, &summary](OriginatorContext& ctx)
    {
        ctx.originateLsa<Policy>(key, summary, false);
    };

    if (sourceAreaId == 0) // Transit area reoriginates to all other normal areas.
    {
        process.forEachOriginCtx([&](uint32_t id, OriginatorContext& ctx) {
            if (id == 0) return;
            if (!AreaFlagManager::getExternalRouting(ctx.getAreaFlags()))
                return;
            processLsa(ctx);
        });
    }
    else if (auto* originCtx = process.getOriginCtx(0); originCtx) // Normal areas reoriginate to Transit area.
    {
        processLsa(*originCtx);
    }
}

template <typename Policy>
void InterOriginator::addExternal(OriginatorContext& ctx, uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!remove)
    {
        addAsbrLsa<Policy>(ctx, asbr);
    }

    auto& external = ctx.asbrExternalRoutes[asbr];

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
            auto& asbrLsa = ctx.asbrLsas[asbr];
            ctx.getIntraOriginator().expire(asbrLsa);
            ctx.asbrExternalRoutes.erase(asbr);
            ctx.asbrLsas.erase(asbr);
        }
    }
}

template <typename Policy>
void InterOriginator::refreshAsbrs(OriginatorContext& ctx)
{
    config::ospf::AreaType type = ctx.area.getType();
    if (type == config::ospf::AreaType::NORMAL)
        for (const auto& asbr : ctx.asbrLsas)
            addAsbrLsa<Policy>(ctx, asbr.first, true);
}

template <typename Policy>
void InterOriginator::refreshStubDefaultOriginate(OriginatorContext& ctx)
{
    config::ospf::AreaType type = ctx.area.getType();

    bool add = ctx.area.process.isABR() && type == config::ospf::AreaType::STUB;

    setStubDefaultOriginate<Policy>(ctx, add);
}

void InterOriginator::refreshNssaDefaultOriginate(OriginatorContext& ctx)
{
    config::ospf::AreaType type = ctx.area.getType();

    bool add = ctx.area.process.isABR() &&
        type == config::ospf::AreaType::NSSA &&
        ctx.getConfigs().get<config::OspfArea::NSSA_DEFAULT_ORIGINATE>().load();

    setNssaDefaultOriginate(ctx, add);
}

uint32_t InterOriginator::fetchAddMonotonicIntraId()
{
    return monotonicIntraId.fetch_add(1, std::memory_order_release);
}

template <typename Policy>
void InterOriginator::setStubDefaultOriginate(OriginatorContext& ctx, bool add)
{
    if (ctx.stubDefaultRoute.has_value() == add)
        return;

    if (!ctx.stubDefaultRoute.has_value())
    {
        LsaKey key;
        key.lsaType = Policy::InterNetworkType;
        key.linkStateId = 0;
        key.advertisingRouter = process.getRouterId();
        ctx.stubDefaultRoute = key;
    }

    auto& info = ctx.originationState[ctx.stubDefaultRoute.value()];
    LsaBody& body = info.body;

    auto costField = ctx.getConfigs().get<config::OspfArea::DEFAULT_COST>();
    uint32_t cost = costField.hasValue() ? costField.load() : 1;

    if constexpr (std::is_same_v<Policy, PolicyV3>)
    {
        if (process.af == types::AddressFamily::IPv4)
        {
            body = InterAreaPrefixLsaV4();
            auto& summary = std::get<InterAreaPrefixLsaV4>(body);
            summary.metric = cost;
            summary.options = 0;
            summary.prefix = types::IPv4Prefix{};
        }
        else
        {
            body = InterAreaPrefixLsa();
            auto& summary = std::get<InterAreaPrefixLsa>(body);
            summary.metric = cost;
            summary.options = 0;
            summary.prefix = types::IPv6Prefix{};
        }
    }
    else
    {
        body = typename Policy::InterNetworkLsa();
        auto& summary = std::get<typename Policy::InterNetworkLsa>(body);
        summary.metric = cost;
        summary.networkMask = 0;
    }

    info.expire = !add;

    ctx.processOriginatedLsa<Policy>(ctx.stubDefaultRoute.value());

    if (!add) ctx.stubDefaultRoute.reset();
}

void InterOriginator::setNssaDefaultOriginate(OriginatorContext& ctx, bool add)
{
    if (ctx.nssaDefaultRoute.has_value() == add)
        return;

    if (!ctx.nssaDefaultRoute.has_value())
        ctx.nssaDefaultRoute = monotonicIntraId.fetch_add(1, std::memory_order_release);

    ExternalOriginateContext extCtx = {
        .lsId = ctx.nssaDefaultRoute.value(),
        .prefix = types::IPPrefix(process.af),
        .metric = ctx.getConfigs().get<config::OspfArea::NSSA_DEFAULT_METRIC>().load(),
        .tag = 0,
        .nextHop = std::nullopt,
        .metricIsE2 = ctx.getConfigs().get<config::OspfArea::NSSA_DEFAULT_METRIC_TYPE>().load()
    };

    if (process.isV3)
    {
        LsaKey key = process.externalOriginator.buildExternalKey<PolicyV3>(extCtx, true);
        auto& ext = ctx.originationState[key];
        ext.body = ExternalLsaV3();
        ext.expire = !add;
        process.externalOriginator.buildExternalBody<PolicyV3>(extCtx, std::get<ExternalLsaV3>(ext.body), true);
        ctx.processOriginatedLsa<PolicyV3>(key);
    }
    else
    {
        LsaKey key = process.externalOriginator.buildExternalKey<PolicyV2>(extCtx, true);
        auto& ext = ctx.originationState[key];
        ext.body = ExternalLsaV2();
        ext.expire = !add;
        process.externalOriginator.buildExternalBody<PolicyV2>(extCtx, std::get<ExternalLsaV2>(ext.body), true);
        ctx.processOriginatedLsa<PolicyV2>(key);
    }

    if (!add) ctx.nssaDefaultRoute.reset();
}

template void InterOriginator::originateSummary<PolicyV2>(OriginatorContext&, uint32_t, const types::IPPrefix&, uint32_t, bool);
template void InterOriginator::originateSummary<PolicyV3>(OriginatorContext&, uint32_t, const types::IPPrefix&, uint32_t, bool);

template void InterOriginator::addAsbrLsa<PolicyV2>(OriginatorContext&, uint32_t, bool);
template void InterOriginator::addAsbrLsa<PolicyV3>(OriginatorContext&, uint32_t, bool);

template void InterOriginator::reoriginateSummaries<PolicyV2>(OriginatorContext&, std::vector<OspfRouteChange>&);
template void InterOriginator::reoriginateSummaries<PolicyV3>(OriginatorContext&, std::vector<OspfRouteChange>&);

template void InterOriginator::reoriginateSummary<PolicyV2>(OriginatorContext&, OspfRouteChange&);
template void InterOriginator::reoriginateSummary<PolicyV3>(OriginatorContext&, OspfRouteChange&);

template void InterOriginator::addExternal<PolicyV2>(OriginatorContext&, uint32_t, uint32_t, bool);
template void InterOriginator::addExternal<PolicyV3>(OriginatorContext&, uint32_t, uint32_t, bool);

template void InterOriginator::refreshAsbrs<PolicyV2>(OriginatorContext&);
template void InterOriginator::refreshAsbrs<PolicyV3>(OriginatorContext&);

template void InterOriginator::refreshStubDefaultOriginate<PolicyV2>(OriginatorContext&);
template void InterOriginator::refreshStubDefaultOriginate<PolicyV3>(OriginatorContext&);

template void InterOriginator::setStubDefaultOriginate<PolicyV2>(OriginatorContext&, bool);
template void InterOriginator::setStubDefaultOriginate<PolicyV3>(OriginatorContext&, bool);
}
