// InterRouteManager.cpp

#include "InterRouteManager.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"
#include "ospf/topology/TopologyTypes.hpp"
#include "ospf/topology/RouteManagerUtility.h"
#include "ospf/spf/SpfTypes.hpp"
#include "ospf/OspfTypes.hpp"

namespace routing::ospf
{
template<typename Policy>
std::pair<types::IPPrefix, std::optional<OspfPath>> InterRouteManager::deriveInterAreaNetwork(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body)
{
    const uint8_t adminDistance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::INTER_AREA_DISTANCE>().load();

    const typename Policy::InterNetworkLsa& summary = std::get<typename Policy::InterNetworkLsa>(body);
    
    types::IPPrefix prefix;
    if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>)
        prefix = {key.linkStateId, static_cast<uint8_t>(std::popcount(summary.networkMask))};
    else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, InterAreaPrefixLsa>)
        prefix = types::IPPrefix(summary.prefix.addr, summary.prefix.prefixLength);

    if (header.age == OSPF_MAX_AGE) return {prefix, std::nullopt};

    auto abrInfo = RouteManagerUtility::resolveToAbrs(area.process, key.advertisingRouter);
    if (!abrInfo.has_value()) return {prefix, std::nullopt}; // ABR not found

    const uint64_t distance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
        ? std::numeric_limits<uint64_t>::max() : abrInfo->cost + summary.metric;

    return {prefix, makePath(area.areaId, 0, adminDistance, distance, std::move(abrInfo->nextHops), OspfRouteType::INTER_AREA)};
}

template <typename Policy>
void InterRouteManager::deriveInterAreaRouter(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body)
{
    const bool remove = header.age == OSPF_MAX_AGE;

    auto abrInfo = RouteManagerUtility::resolveToAbrs(area.process, key.advertisingRouter);
    if (!abrInfo.has_value()) return;

    const typename Policy::InterRouterLsa& asbr = std::get<typename Policy::InterRouterLsa>(body);
    uint64_t distance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
        ? std::numeric_limits<uint64_t>::max() : abrInfo->cost + asbr.metric;

    RouteManagerUtility::getTopoTable(area.process).updateAreaAsbr(area.areaId, OspfRouter{key.linkStateId, distance, std::move(abrInfo->nextHops)}, remove);
}

template<typename Policy>
void InterRouteManager::deriveInterAreaRoutes(const SpfResult& spf, std::vector<std::pair<types::IPPrefix, OspfPath>>& out, Area& area)
{
    auto& lsdb = RouteManagerUtility::getLsdb(area);
    const uint8_t adminDistance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::INTER_AREA_DISTANCE>().load();

    NhCache nhCache;
    out.reserve(out.size() + spf.confirmedOrder.size());

    constexpr uint32_t networkType = std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>
        ? OSPFV2_LSA_SUM_NET : OSPFV3_LSA_INTER_AREA_PREFIX;
    constexpr uint32_t routerType = std::is_same_v<std::remove_cv_t<typename Policy::InterRouterLsa>, SummaryRouterLsa>
        ? OSPFV2_LSA_SUM_ASBR : OSPFV3_LSA_INTER_AREA_ROUTER;

    lsdb.forEachInType(networkType, [&](const LsaKey& key, const LsaRecord& record)
    {
        if (!std::holds_alternative<typename Policy::InterNetworkLsa>(record.body) || record.header.age == OSPF_MAX_AGE) return;

        auto abrInfo = RouteManagerUtility::resolveToAbr(area, spf, key.advertisingRouter, nhCache);
        if (!abrInfo.has_value()) return; // ABR not found

        const typename Policy::InterNetworkLsa& summary = std::get<typename Policy::InterNetworkLsa>(record.body);
        const uint64_t distance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
            ? std::numeric_limits<uint64_t>::max() : abrInfo->first + summary.metric;

        if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>)
            out.emplace_back(types::IPPrefix{key.linkStateId, static_cast<uint8_t>(std::popcount(summary.networkMask))},
                makePath(area.areaId, 0, adminDistance, distance, abrInfo->second, OspfRouteType::INTER_AREA));
        else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, InterAreaPrefixLsa>)
            out.emplace_back(types::IPPrefix(summary.prefix.addr, summary.prefix.prefixLength), makePath(area.areaId, summary.options, adminDistance, distance, abrInfo->second, OspfRouteType::INTER_AREA));
    });

    std::vector<OspfRouter> asbrs;

    lsdb.forEachInType(routerType, [&](const LsaKey& key, const LsaRecord& record)
    {
        if (!std::holds_alternative<typename Policy::InterRouterLsa>(record.body) || record.header.age == OSPF_MAX_AGE) return;

        auto abrInfo = RouteManagerUtility::resolveToAbr(area, spf, key.advertisingRouter, nhCache);
        if (!abrInfo.has_value()) return;

        const typename Policy::InterRouterLsa& asbr = std::get<typename Policy::InterRouterLsa>(record.body);
        const uint64_t distance = RouteManagerUtility::getProcessConfigs(area.process).get<config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
            ? std::numeric_limits<uint64_t>::max() : abrInfo->first + asbr.metric;

        asbrs.emplace_back(key.linkStateId, distance, abrInfo->second);
    });

    if (!asbrs.empty())
        RouteManagerUtility::getTopoTable(area.process).updateAreaAsbrs(area.areaId, asbrs);
}

template std::pair<types::IPPrefix, std::optional<OspfPath>> InterRouteManager::deriveInterAreaNetwork<PolicyV2>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);
template std::pair<types::IPPrefix, std::optional<OspfPath>> InterRouteManager::deriveInterAreaNetwork<PolicyV3>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);

template void InterRouteManager::deriveInterAreaRouter<PolicyV2>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);
template void InterRouteManager::deriveInterAreaRouter<PolicyV3>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);

template void InterRouteManager::deriveInterAreaRoutes<PolicyV2>(const SpfResult&, std::vector<std::pair<types::IPPrefix, OspfPath>>&, Area&);
template void InterRouteManager::deriveInterAreaRoutes<PolicyV3>(const SpfResult&, std::vector<std::pair<types::IPPrefix, OspfPath>>&, Area&);
}
