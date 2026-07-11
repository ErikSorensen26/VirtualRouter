// ExternalRouteManager.cpp

#include "ExternalRouteManager.h"
#include "ospf/OspfProcess.h"
#include "ospf/topology/TopologyTypes.hpp"
#include "ospf/topology/RouteManagerUtility.h"
#include "ospf/OspfTypes.hpp"
#include "core/routing/RoutingTable.hpp"
#include "utils/RCU.hpp"
#include "VirtualRouter.h"

namespace routing::ospf
{
ExternalRouteManager::ExternalRouteManager(OspfProcess& proc)
    : process(proc)
{}

struct ExtRec
{
    types::IPPrefix prefix{};
    types::IPAddress fwd{};
    uint8_t options{0};
    uint32_t asbrRid{0};
    uint32_t metric{0};
    bool isType2{false};
};

template <typename AddrT>
std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveInternalAddress(AddrT addr, core::RoutingTable& globalRib)
{
    utils::RCU::Guard g;
    core::RibEntry<AddrT>* r = globalRib.lookup<AddrT>(addr, g);
    if (!r || r->nextHopCount == 0) return std::nullopt;

    std::vector<OspfNextHop> hops;

    for (size_t i = 0; i < r->nextHopCount; i++)
    {
        auto& hop = r->nextHops[i];
        hops.push_back(OspfNextHop{hop.iface.getId(), types::IPAddress{hop.nextHop.value()}});
    }

    return std::make_pair(r->metric, hops);
};

template<typename Policy>
std::pair<types::IPPrefix, std::optional<OspfPath>> ExternalRouteManager::deriveExternalRoute(const LsaKey& key, const std::pair<LsaHeader, LsaBody>& rec)
{
    const uint8_t adminDistance = RouteManagerUtility::getProcessConfigs(process).get<config::Ospf::EXTERNAL_DISTANCE>().load();
    const uint32_t selfRid       = process.getRouterId();

    using EL = std::remove_cv_t<typename Policy::ExternalLsa>;

    constexpr uint16_t kMaxAge = OSPF_MAX_AGE;

    auto& globalRib = process.routingInstance->getRib();

    const typename Policy::ExternalLsa& extLsa = std::get<typename Policy::ExternalLsa>(rec.second);

    ExtRec ext{};

    if constexpr (std::is_same_v<EL, ExternalLsaV2>)
    {
        ext.prefix = types::IPPrefix{key.linkStateId, static_cast<uint8_t>(std::popcount(extLsa.networkMask))};
        ext.fwd = extLsa.forwardingAddress;
        ext.metric = extLsa.metric;
        ext.isType2 = extLsa.isType2;
        ext.options = 0;
    }
    else
    {
        ext.prefix = types::IPPrefix(extLsa.prefix.addr, extLsa.prefix.prefixLength);
        ext.fwd = extLsa.forwardingAddress.has_value() ? types::IPAddress(extLsa.forwardingAddress->addr) : types::IPAddress(__uint128_t{0});
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

    if (ext.fwd != types::IPAddress{})
    {
        if constexpr (std::is_same_v<EL, ExternalLsaV2>)
        {
            const uint32_t fwdAddr = ext.fwd.v4();
            if (auto res = resolveInternalAddress(fwdAddr, globalRib); res.has_value())
            {
                X = res->first;
                nh = std::move(res->second);
                anchored = true;
            }
        }
        else
        {
            const __uint128_t fwdAddr = ext.fwd.v6();
            if (auto res = resolveInternalAddress(fwdAddr, globalRib); res.has_value())
            {
                X = res->first;
                nh = std::move(res->second);
                anchored = true;
            }
        }
    }

    if (!anchored)
    {
        const auto* rr = RouteManagerUtility::getTopoTable(process).lookup(asbrRid);
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
std::vector<std::pair<types::IPPrefix, OspfPath>> ExternalRouteManager::deriveExternalRoutes()
{
    const uint8_t adminDistance = RouteManagerUtility::getProcessConfigs(process).get<config::Ospf::EXTERNAL_DISTANCE>().load();
    const uint32_t selfRid       = process.getRouterId();

    using EL = std::remove_cv_t<typename Policy::ExternalLsa>;

    constexpr uint16_t kMaxAge = OSPF_MAX_AGE;

    std::vector<std::pair<types::IPPrefix, OspfPath>> out;
    out.reserve(64);

    auto& globalRib = process.routingInstance->getRib();

    for (auto& [key, rec] : RouteManagerUtility::getExtOriginator(process).externalDb)
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
            ext.prefix = types::IPPrefix{key.linkStateId, static_cast<uint8_t>(std::popcount(extLsa.networkMask))};
            ext.fwd = extLsa.forwardingAddress;
            ext.metric = extLsa.metric;
            ext.isType2 = extLsa.isType2;
            ext.options = 0;
        }
        else
        {
            ext.prefix = types::IPPrefix(extLsa.prefix.addr, extLsa.prefix.prefixLength);
            ext.fwd = extLsa.forwardingAddress.has_value() ? types::IPAddress(extLsa.forwardingAddress->addr) : types::IPAddress(__uint128_t{0});
            ext.metric = extLsa.metric;
            ext.isType2 = extLsa.isType2;
            ext.options = extLsa.options;
        }

        uint64_t X = 0;
        std::vector<OspfNextHop> nh;

        bool anchored = false;

        if (ext.fwd != types::IPAddress{})
        {
            if constexpr (std::is_same_v<EL, ExternalLsaV2>)
            {
                const uint32_t fwdAddr = ext.fwd.v4();
                if (auto res = resolveInternalAddress(fwdAddr, globalRib); res.has_value())
                {
                    X = res->first;
                    nh = std::move(res->second);
                    anchored = true;
                }
            }
            else
            {
                const __uint128_t fwdAddr = ext.fwd.v6();
                if (auto res = resolveInternalAddress(fwdAddr, globalRib); res.has_value())
                {
                    X = res->first;
                    nh = std::move(res->second);
                    anchored = true;
                }
            }
        }

        if (!anchored)
        {
            const auto* rr = RouteManagerUtility::getTopoTable(process).lookup(asbrRid);
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

template std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveInternalAddress<uint32_t>(uint32_t, core::RoutingTable&);
template std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> resolveInternalAddress<__uint128_t>(__uint128_t, core::RoutingTable&);

template std::pair<types::IPPrefix, std::optional<OspfPath>> ExternalRouteManager::deriveExternalRoute<PolicyV2>(const LsaKey&, const std::pair<LsaHeader, LsaBody>&);
template std::pair<types::IPPrefix, std::optional<OspfPath>> ExternalRouteManager::deriveExternalRoute<PolicyV3>(const LsaKey&, const std::pair<LsaHeader, LsaBody>&);

template std::vector<std::pair<types::IPPrefix, OspfPath>> ExternalRouteManager::deriveExternalRoutes<PolicyV2>();
template std::vector<std::pair<types::IPPrefix, OspfPath>> ExternalRouteManager::deriveExternalRoutes<PolicyV3>();
}
