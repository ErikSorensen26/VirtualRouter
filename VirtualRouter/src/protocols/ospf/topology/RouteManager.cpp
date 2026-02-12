// OspfRouteManager.cpp

#include <VirtualRouter.h>

#include "RouteManager.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/OspfTypes.hpp"
#include "routing/RoutingTable.hpp"
#include "RoutingTable.h"

#include "ospf/ospfv3/database/IntraAreaPrefixLsa.hpp"

namespace OSPF
{
struct ExtRec
{
    IPPrefix prefix{};
    IPAddress fwd{};
    uint8_t options{0};
    uint32_t asbrRid{0};
    uint32_t metric{0};
    bool isType2{false};
};

struct SelState
{
    bool set = false;
    bool isType2 = false;
    uint64_t installedCost = 0;  // For E2, this is Y; for E1, X+Y
    uint64_t tieX = 0;           // Only meaningful for E2
    uint8_t options = 0;
    std::vector<OspfNextHop> nextHops;
};

OspfPath RouteManager::makePath(
    std::optional<uint32_t> areaId,
    uint8_t options,
    uint8_t adminDistance,
    uint64_t cost,
    std::vector<OspfNextHop> nextHops,
    OspfRouteType type)
{
    return OspfPath{
        .type = type,
        .area = areaId,
        .cost = cost,
        .adminDistance = adminDistance,
        .options = options,
        .nextHops = std::move(nextHops)
    };
}

std::optional<OspfNextHop> resolveDirectNextHop(Area& area, const Vertex& v, const ParentRef& pref)
{
    // Get RID of destination router.
    uint32_t rid = v.type == VertexType::ROUTER
        ? static_cast<uint32_t>(v.id)
        : networkAdvRouter(v.id);

    auto& ifaceMgr = area.process().getIfaceMgr();

    OspfInterface* iface = ifaceMgr.getInterface({pref.firstHopIfid, area.areaId});
    if (!iface) return std::nullopt;

    Neighbor* nbr = iface->getNTable().lookup(rid);
    if (!nbr) return std::nullopt;

    // Return neighbors IP address.
    return OspfNextHop{pref.firstHopIfid, nbr->ipAddress};
}

void dedupeNextHops(std::vector<OspfNextHop>& hops)
{
    std::sort(hops.begin(), hops.end(),
        [](const OspfNextHop& a, const OspfNextHop& b)
        {
            if (a.interfaceId != b.interfaceId) return a.interfaceId < b.interfaceId;
            return a.nextHop < b.nextHop;
        });

    hops.erase(std::unique(hops.begin(), hops.end()), hops.end());
}

std::vector<OspfNextHop> computeNextHops(Area& area, const Vertex& v, const SpfResult& spf, RouteManager::NhCache& cache)
{
    if (auto it = cache.find(v); it != cache.end())
        return it->second;

    std::vector<OspfNextHop> result;

    const auto nodeIt = spf.nodes.find(v);
    if (nodeIt == spf.nodes.end())
    {
        cache[v] = result;
        return result;
    }

    const auto& node = nodeIt->second;

    // Resolve parents
    for (const auto& pref : node.parents)
    {
        const Vertex& parent = pref.parent;

        if (parent == spf.root)
        {
            if (auto nh = resolveDirectNextHop(area, v, pref))
                result.push_back(*nh);
            continue;
        }

        auto parentNhs = computeNextHops(area, parent, spf, cache);
        result.insert(result.end(), parentNhs.begin(), parentNhs.end());
    }

    dedupeNextHops(result);
    cache[v] = result;
    return result;
}

template <typename Policy>
void RouteManager::deriveIntraAreaRoutes(const SpfResult& spf, std::vector<std::pair<IPPrefix, OspfPath>>& out, Area& area)
{
    auto& lsdb = area.lsdb();
    const uint8_t adminDistance = area.process().getConfigs().get<Config::Ospf::INTRA_AREA_DISTANCE>().load();

    NhCache nhCache;
    out.reserve(out.size() + spf.confirmedOrder.size());

    auto ranges = area.getRanges();

    auto collectIntraAreaPrefixFragments = [&](const LsaKey& refKey) -> std::vector<IntraAreaPrefix>
    {
        std::vector<IntraAreaPrefix> prefixes;

        lsdb.forEachInAdv(refKey, [&](uint32_t, const LsaRecord& record)
        {
            if (!std::holds_alternative<IntraAreaPrefixLsa>(record.body)) return;

            const IntraAreaPrefixLsa& frag = std::get<IntraAreaPrefixLsa>(record.body);

            if (frag.referencedAdvRouter != refKey.advertisingRouter) return;
            if (frag.referencedLsaType != refKey.lsaType) return;

            prefixes.insert(prefixes.end(), frag.prefixes.begin(), frag.prefixes.end());
        });

        return prefixes;
    };

    for (const Vertex& v : spf.confirmedOrder)
    {
        if (v == spf.root)
            continue;

        const auto& nodeIt = spf.nodes.find(v);
        if (nodeIt == spf.nodes.end()) continue;

        const auto& node = nodeIt->second;

        auto nextHops = computeNextHops(area, v, spf, nhCache);
        if (nextHops.empty()) continue;

        if (v.type == VertexType::NETWORK)
        {
            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV2>)
            {
                const uint16_t lsaType = OSPFV2_LSA_NETWORK;
                const LsaKey key = networkLsaKey(v.id, lsaType);

                auto* routerLsa = lsdb.find(key);
                if (!routerLsa || !std::holds_alternative<typename Policy::RouterLsa>(routerLsa->body))
                    continue;

                const auto& body = std::get<NetworkLsaV2>(routerLsa->body);

                const uint8_t plen = static_cast<uint8_t>(std::popcount(body.networkMask));
                const IPPrefix prefix{ key.linkStateId, plen };

                out.emplace_back(prefix, makePath(
                        area.areaId, 0,
                        adminDistance, node.dist,
                        std::move(nextHops),
                        OspfRouteType::INTRA_AREA));
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV3>)
            {
                const uint16_t refType = OSPFV3_LSA_INTRA_AREA_PREFIX;
                const LsaKey& refKey = networkLsaKey(v.id, refType);

                auto prefixes = collectIntraAreaPrefixFragments(refKey);
                for (const auto& pr : prefixes)
                {
                    out.emplace_back(pr.prefix, makePath(
                            area.areaId, pr.options,
                            adminDistance, node.dist + pr.metric,
                            nextHops, OspfRouteType::INTRA_AREA));
                }
            }
            continue;
        }

        if (v.type == VertexType::ROUTER)
        {
            if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::RouterLsa>, RouterLsaV2>)
            {
                const uint16_t lsaType = OSPFV2_LSA_ROUTER;
                const LsaKey key(lsaType, static_cast<uint32_t>(v.id), static_cast<uint32_t>(v.id));

                auto* rec = lsdb.find(key);
                if (!rec || !std::holds_alternative<typename Policy::RouterLsa>(rec->body))
                    continue;

                const auto& links = std::get<typename Policy::RouterLsa>(rec->body).links;

                for (const auto& link : links)
                {
                    if (link.type != OSPFV2_LINK_STUB) continue;

                    const uint8_t plen = static_cast<uint8_t>(std::popcount(link.linkData));
                    const IPPrefix prefix{ link.linkId, plen };
                    
                    out.emplace_back(prefix, makePath(
                            area.areaId, 0,
                            adminDistance, link.metric,
                            nextHops, OspfRouteType::INTRA_AREA));
                }
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::RouterLsa>, RouterLsaV3>)
            {
                const uint16_t refType = OSPFV3_LSA_INTRA_AREA_PREFIX;
                const LsaKey refKey(refType, static_cast<uint32_t>(v.id), static_cast<uint32_t>(v.id));

                auto prefixes = collectIntraAreaPrefixFragments(refKey);
                for (const auto& pr : prefixes)
                {
                    out.emplace_back(pr.prefix, makePath(
                            area.areaId, pr.options,
                            adminDistance, pr.metric,
                            nextHops, OspfRouteType::INTRA_AREA));
                }
            }

            continue;
        }
    }
}

static std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveToAbr(Area& area, const SpfResult& spf, uint32_t abrRid, RouteManager::NhCache& nhCache)
{
    const Vertex abrV{ VertexType::ROUTER, abrRid };

    const auto nodeIt = spf.nodes.find(abrV);
    if (nodeIt == spf.nodes.end())
        return std::nullopt;

    auto nh = computeNextHops(area, abrV, spf, nhCache);
    if (nh.empty())
        return std::nullopt;

    const uint64_t dist = nodeIt->second.dist;
    return std::make_pair(dist, std::move(nh));
}

static std::optional<RouterReach> resolveToAbrs(OspfProcess& topo, uint32_t abrRid)
{
    auto* res = topo.table.lookup(abrRid);
    if (res) return *res;
    return std::nullopt;
}

template<typename Policy>
std::pair<IPPrefix, std::optional<OspfPath>> RouteManager::deriveInterAreaNetwork(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body)
{
    const uint8_t adminDistance = area.process().getConfigs().get<Config::Ospf::INTER_AREA_DISTANCE>().load();

    const typename Policy::InterNetworkLsa& summary = std::get<typename Policy::InterNetworkLsa>(body);
    
    IPPrefix prefix;
    if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>)
        prefix = {key.linkStateId, static_cast<uint8_t>(std::popcount(summary.networkMask))};
    else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, InterAreaPrefixLsa>)
        prefix = summary.prefix;

    if (header.age == OSPF_MAX_AGE) return {prefix, std::nullopt};

    auto abrInfo = resolveToAbrs(area.process(), key.advertisingRouter);
    if (!abrInfo.has_value()) return {prefix, std::nullopt}; // ABR not found

    const uint64_t distance = area.process().getConfigs().get<Config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
        ? std::numeric_limits<uint64_t>::max() : abrInfo->cost + summary.metric;

    return {prefix, makePath(area.areaId, 0, adminDistance, distance, std::move(abrInfo->nextHops), OspfRouteType::INTER_AREA)};
}

template <typename Policy>
void RouteManager::deriveInterAreaRouter(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body)
{
    const bool remove = header.age == OSPF_MAX_AGE;

    auto abrInfo = resolveToAbrs(area.process(), key.advertisingRouter);

    const typename Policy::InterRouterLsa& asbr = std::get<typename Policy::InterRouterLsa>(body);
    uint64_t distance = area.process().getConfigs().get<Config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
        ? std::numeric_limits<uint64_t>::max() : abrInfo->cost + asbr.metric;

    area.process().table.updateAreaAsbr(area.areaId, OspfRouter{key.linkStateId, distance, std::move(abrInfo->nextHops)}, remove);
}

template<typename Policy>
void RouteManager::deriveInterAreaRoutes(const SpfResult& spf, std::vector<std::pair<IPPrefix, OspfPath>>& out, Area& area)
{
    auto& lsdb = area.lsdb();
    const uint8_t adminDistance = area.process().getConfigs().get<Config::Ospf::INTER_AREA_DISTANCE>().load();

    NhCache nhCache;
    out.reserve(out.size() + spf.confirmedOrder.size());

    constexpr uint32_t networkType = std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>
        ? OSPFV2_LSA_SUM_NET : OSPFV3_LSA_INTER_AREA_PREFIX;
    constexpr uint32_t routerType = std::is_same_v<std::remove_cv_t<typename Policy::InterRouterLsa>, SummaryRouterLsa>
        ? OSPFV2_LSA_SUM_ASBR : OSPFV3_LSA_INTER_AREA_ROUTER;

    lsdb.forEachInType(networkType, [&](const LsaKey& key, const LsaRecord& record)
    {
        if (!std::holds_alternative<typename Policy::InterNetworkLsa>(record.body) || record.header.age == OSPF_MAX_AGE) return;

        auto abrInfo = resolveToAbr(area, spf, key.advertisingRouter, nhCache);
        if (!abrInfo.has_value()) return; // ABR not found

        const typename Policy::InterNetworkLsa& summary = std::get<typename Policy::InterNetworkLsa>(record.body);
        const uint64_t distance = area.process().getConfigs().get<Config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
            ? std::numeric_limits<uint64_t>::max() : abrInfo->first + summary.metric;

        if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, SummaryNetworkLsa>)
            out.emplace_back(IPPrefix{key.linkStateId, static_cast<uint8_t>(std::popcount(summary.networkMask))},
                makePath(area.areaId, 0, adminDistance, distance, abrInfo->second, OspfRouteType::INTER_AREA));
        else if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::InterNetworkLsa>, InterAreaPrefixLsa>)
            out.emplace_back(summary.prefix, makePath(area.areaId, summary.options, adminDistance, distance, abrInfo->second, OspfRouteType::INTER_AREA));
    });

    std::vector<OspfRouter> asbrs;

    lsdb.forEachInType(routerType, [&](const LsaKey& key, const LsaRecord& record)
    {
        if (!std::holds_alternative<typename Policy::InterNetworkLsa>(record.body) || record.header.age == OSPF_MAX_AGE) return;

        auto abrInfo = resolveToAbr(area, spf, key.advertisingRouter, nhCache);

        const typename Policy::InterRouterLsa& asbr = std::get<typename Policy::InterRouterLsa>(record.body);
        const uint64_t distance = area.process().getConfigs().get<Config::Ospf::MAX_METRIC_SUMMARY_LSA>().load()
            ? std::numeric_limits<uint64_t>::max() : abrInfo->first + asbr.metric;

        asbrs.emplace_back(key.linkStateId, distance, abrInfo->second);
    });

    if (!asbrs.empty())
        area.process().table.updateAreaAsbrs(area.areaId, asbrs);
}

template <typename AddrT>
static std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveInternalAddress(AddrT addr, OspfProcess& process, RoutingTable& globalRib)
{
    RibEntry<AddrT>* r = globalRib.lookup<AddrT>(addr, process.getProcId(), RouteSource::OSPF_INTRA);
    if (!r) r = globalRib.lookup<AddrT>(addr, process.getProcId(), RouteSource::OSPF_INTER);
    if (!r || r->nextHopCount == 0) return std::nullopt;

    std::vector<OspfNextHop> hops;

    for (size_t i = 0; i < r->nextHopCount; i++)
    {
        auto& hop = r->nextHops[i];
        hops.push_back(OspfNextHop{hop.iface, IPAddress{hop.nextHop.value()}});
    }

    return std::make_pair(r->metric, hops);
};

template<typename Policy>
std::pair<IPPrefix, std::optional<OspfPath>> RouteManager::deriveExternalRoute(OspfProcess& process, const LsaKey& key, const std::pair<LsaHeader, LsaBody>& rec)
{
    const uint8_t adminDistance = process.getConfigs().get<Config::Ospf::EXTERNAL_DISTANCE>().load();
    const uint32_t selfRid       = process.getRouterId();

    using EL = std::remove_cv_t<typename Policy::ExternalLsa>;

    constexpr uint16_t kMaxAge = OSPF_MAX_AGE;

    auto& globalRib = process.routingInstance->getRib();

    const typename Policy::ExternalLsa& extLsa = std::get<typename Policy::ExternalLsa>(rec.second);

    ExtRec ext{};

    if constexpr (std::is_same_v<EL, ExternalLsaV2>)
    {
        ext.prefix = IPPrefix{key.linkStateId, static_cast<uint8_t>(std::popcount(extLsa.networkMask))};
        ext.fwd = extLsa.forwardingAddress;
        ext.metric = extLsa.metric;
        ext.isType2 = extLsa.isType2;
        ext.options = 0;
    }
    else
    {
        ext.prefix = extLsa.prefix;
        ext.fwd = extLsa.forwardingAddress.has_value() ? extLsa.forwardingAddress.value() : uint32_t{0};
        ext.metric = extLsa.metric;
        ext.isType2 = extLsa.isType2;
        ext.options = extLsa.options;
    }

    if (rec.first.age == kMaxAge)
        return {ext.prefix, std::nullopt};

    const uint32_t asbrRid = key.advertisingRouter;
    if (asbrRid == 0 || asbrRid == selfRid)
        return {ext.prefix, std::nullopt};

    uint64_t X = 0;
    std::vector<OspfNextHop> nh;

    bool anchored = false;

    if (ext.fwd != IPAddress{})
    {
        if constexpr (std::is_same_v<EL, ExternalLsaV2>)
        {
            const uint32_t fwdAddr = readU32(ext.fwd.raw);
            if (auto res = resolveInternalAddress(fwdAddr, process, globalRib); res.has_value())
            {
                X = res->first;
                nh = std::move(res->second);
                anchored = true;
            }
        }
        else
        {
            const __uint128_t fwdAddr = readU128(ext.fwd.raw);
            if (auto res = resolveInternalAddress(fwdAddr, process, globalRib); res.has_value())
            {
                X = res->first;
                nh = std::move(res->second);
                anchored = true;
            }
        }
    }

    if (!anchored)
    {
        const auto* rr = process.table.lookup(asbrRid);
        if (!rr || rr->nextHops.empty())
            return {ext.prefix, std::nullopt};

        X = rr->cost;
        nh = rr->nextHops;
        anchored = true;
    }

    if (!anchored || nh.empty())
        return {ext.prefix, std::nullopt};

    const uint64_t Y = static_cast<uint64_t>(ext.metric);
    const uint64_t installed = ext.isType2 ? Y : (X + Y);

    return {ext.prefix, makePath(std::nullopt, ext.options, adminDistance, installed, std::move(nh), OspfRouteType::EXTERNAL)};
}

template<typename Policy>
std::vector<std::pair<IPPrefix, OspfPath>> RouteManager::deriveExternalRoutes(OspfProcess& process)
{
    const uint8_t adminDistance = process.getConfigs().get<Config::Ospf::EXTERNAL_DISTANCE>().load();
    const uint32_t selfRid       = process.getRouterId();

    using EL = std::remove_cv_t<typename Policy::ExternalLsa>;

    constexpr uint16_t kMaxAge = OSPF_MAX_AGE;

    std::vector<std::pair<IPPrefix, OspfPath>> out;
    out.reserve(64);

    auto& globalRib = process.routingInstance->getRib();

    for (auto& [key, rec] : process.externalDb)
    {
        if (rec.first.age == kMaxAge)
            continue;

        const uint32_t asbrRid = key.advertisingRouter;
        if (asbrRid == 0 || asbrRid == selfRid)
            continue;

        const typename Policy::ExternalLsa& extLsa = std::get<typename Policy::ExternalLsa>(rec.second);

        ExtRec ext{};

        if constexpr (std::is_same_v<EL, ExternalLsaV2>)
        {
            ext.prefix = IPPrefix{key.linkStateId, static_cast<uint8_t>(std::popcount(extLsa.networkMask))};
            ext.fwd = extLsa.forwardingAddress;
            ext.metric = extLsa.metric;
            ext.isType2 = extLsa.isType2;
            ext.options = 0;
        }
        else
        {
            ext.prefix = extLsa.prefix;
            ext.fwd = extLsa.forwardingAddress.has_value() ? extLsa.forwardingAddress.value() : uint32_t{0};
            ext.metric = extLsa.metric;
            ext.isType2 = extLsa.isType2;
            ext.options = extLsa.options;
        }

        uint64_t X = 0;
        std::vector<OspfNextHop> nh;

        bool anchored = false;

        if (ext.fwd != IPAddress{})
        {
            if constexpr (std::is_same_v<EL, ExternalLsaV2>)
            {
                const uint32_t fwdAddr = readU32(ext.fwd.raw);
                if (auto res = resolveInternalAddress(fwdAddr, process, globalRib); res.has_value())
                {
                    X = res->first;
                    nh = std::move(res->second);
                    anchored = true;
                }
            }
            else
            {
                const __uint128_t fwdAddr = readU128(ext.fwd.raw);
                if (auto res = resolveInternalAddress(fwdAddr, process, globalRib); res.has_value())
                {
                    X = res->first;
                    nh = std::move(res->second);
                    anchored = true;
                }
            }
        }

        if (!anchored)
        {
            const auto* rr = process.table.lookup(asbrRid);
            if (!rr || rr->nextHops.empty())
                continue;

            X = rr->cost;
            nh = rr->nextHops;
            anchored = true;
        }

        if (!anchored || nh.empty())
            continue;

        const uint64_t Y = static_cast<uint64_t>(ext.metric);
        const uint64_t installed = ext.isType2 ? Y : (X + Y);

        out.emplace_back(ext.prefix, makePath(std::nullopt, ext.options, adminDistance, installed, std::move(nh), OspfRouteType::EXTERNAL));
    }

    return out;
}

template void RouteManager::deriveIntraAreaRoutes<PolicyV2>(const SpfResult&, std::vector<std::pair<IPPrefix, OspfPath>>&, Area&);
template void RouteManager::deriveIntraAreaRoutes<PolicyV3>(const SpfResult&, std::vector<std::pair<IPPrefix, OspfPath>>&, Area&);

template std::pair<IPPrefix, std::optional<OspfPath>> RouteManager::deriveInterAreaNetwork<PolicyV2>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);
template std::pair<IPPrefix, std::optional<OspfPath>> RouteManager::deriveInterAreaNetwork<PolicyV3>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);

template void RouteManager::deriveInterAreaRouter<PolicyV2>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);
template void RouteManager::deriveInterAreaRouter<PolicyV3>(Area&, const LsaKey&, const LsaHeader&, const LsaBody&);

template void RouteManager::deriveInterAreaRoutes<PolicyV2>(const SpfResult&, std::vector<std::pair<IPPrefix, OspfPath>>&, Area&);
template void RouteManager::deriveInterAreaRoutes<PolicyV3>(const SpfResult&, std::vector<std::pair<IPPrefix, OspfPath>>&, Area&);

template std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveInternalAddress<uint32_t>(uint32_t, OspfProcess&, RoutingTable&);
template std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveInternalAddress<__uint128_t>(__uint128_t, OspfProcess&, RoutingTable&);

template std::pair<IPPrefix, std::optional<OspfPath>> RouteManager::deriveExternalRoute<PolicyV2>(OspfProcess&, const LsaKey&, const std::pair<LsaHeader, LsaBody>&);
template std::pair<IPPrefix, std::optional<OspfPath>> RouteManager::deriveExternalRoute<PolicyV3>(OspfProcess&, const LsaKey&, const std::pair<LsaHeader, LsaBody>&);

template std::vector<std::pair<IPPrefix, OspfPath>> RouteManager::deriveExternalRoutes<PolicyV2>(OspfProcess&);
template std::vector<std::pair<IPPrefix, OspfPath>> RouteManager::deriveExternalRoutes<PolicyV3>(OspfProcess&);
}
