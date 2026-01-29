// OspfProcess.cpp

#include <Registry.hpp>
#include "OspfProcess.h"
#include <OspfArea.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <OspfRouteManager.h>

namespace OSPF
{
OspfProcess::OspfProcess(bool isV3, uint32_t procId, AddressFamily af, VirtualRouter* vrf)
    : isV3(isV3), routingInstance(vrf), tmgr(vrf->global.timeManager), rib(*this), procId(procId), af(af), ifaceMgr(*this),
    configs([this, isV3]() {
        auto& registry = routingInstance->global.registry;
        auto key = Config::generateOspfKey(routingInstance->instanceId, getProcId(), getAF(), isV3);
        if (isV3)
        {
            if (routingInstance->isDefault)
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
{}

OspfArea* OspfProcess::getArea(uint32_t areaId)
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    auto it = areas.find(areaId);
    if (it == areas.end()) return nullptr;
    return &it->second;
}
    
OspfArea& OspfProcess::insureArea(uint32_t areaId)
{
    std::unique_lock<std::shared_mutex> lock(areaMu);
    if (!areas.contains(areaId))
    {
        auto a = areas.try_emplace(areaId, *this, areaId); // TODO: maybe add pmr
        if (a.second) areaSize.store(areas.size(), std::memory_order_release);
        isABR.store(areas.size() > 1 && areas.contains(0), std::memory_order_release);
    }
    return areas.at(areaId);
}

template <typename Policy>
void OspfProcess::distributeExternalLsa(const OspfArea& sourceArea, IncomingLsaContext& ctx, LsaBody& body)
{
    {
        std::shared_lock<std::shared_mutex> lock(areaMu);
        for (auto& [targetAreaId, targetArea] : areas)
        {
            if (targetAreaId == sourceArea.areaId)
                continue;

            if (ctx.key.lsaType == Policy::NssaType &&
                (sourceArea.type == AreaType::NSSA ||
                 sourceArea.type == AreaType::TOTALLY_NSSA) &&
                targetArea.type == AreaType::NORMAL)
            {
                targetArea.getOriginator().translateNssaToExternal(ctx.key, body);
                continue;
            }

            if (ctx.key.lsaType == Policy::ExternalType && sourceArea.type == AreaType::NORMAL)
            {
                LsaBody bodyCopy = body;
                targetArea.processExternalLsa(ctx, bodyCopy);
            }
        }
    }

    std::pair<IPPrefix, std::optional<OspfPath>> result;
    {
        std::lock_guard<std::mutex> lock(externalMu);

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
}

template <typename Policy>
void OspfProcess::originateExternal(Policy::ExternalLsa& lsa)
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    for (auto& [id, area] : areas)
    {
        LsaKey key{};
        if (area.type == AreaType::NSSA || area.type == AreaType::TOTALLY_NSSA)
        {
            // TODO handle nssa fa suppression
            if (area.getConfigs().get<Config::OspfArea::NSSA_NO_REDISTRIBUTION>().load())
                continue;

            if constexpr (std::is_same_v<Policy, PolicyV3>)
                lsa.options |= (1 << 3); // Enable Propagate bit
            key.lsaType = Policy::NssaLsa;
        }
        if (area.type == AreaType::NORMAL)
        {
            if constexpr (std::is_same_v<Policy, PolicyV3>)
                lsa.options &= ~(1 << 3); // Clear Propagate bit
            key.lsaType = OSPFV2_LSA_EXTERNAL;
        }
        else continue; // Stub Areas do not allow external LSAs

        area.getOriginator().processOriginatedLsa<Policy>(key, lsa);
    }
}

template<typename Policy>
void OspfProcess::flood()
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    for (auto& [_, area] : areas)
        area.flood<Policy>();
}

template <typename Policy>
void OspfProcess::reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList)
{
    if (!isABR.load(std::memory_order_relaxed) || areaSize.load(std::memory_order_relaxed) == 1) return;

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
            key.linkStateId = readU32(path.prefix.addr);
            key.lsaType = OSPFV3_LSA_INTER_AREA_PREFIX;
        }
    }

    {
        std::shared_lock<std::shared_mutex> lock(areaMu);
        uint32_t sourceAreaId = sourceArea.areaId;

        auto processLsas = [&](OspfArea& a)
        {
            for (auto& [key, network] : networks)
                a.getOriginator().processReoriginatedLsa<Policy>(key, network);
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

template void OspfProcess::distributeExternalLsa<PolicyV2>(uint32_t, IncomingLsaContext&, LsaBody&);
template void OspfProcess::distributeExternalLsa<PolicyV3>(uint32_t, IncomingLsaContext&, LsaBody&);

template void OspfProcess::flood<PolicyV2>();
template void OspfProcess::flood<PolicyV3>();

template void OspfProcess::reoriginateSummaries<PolicyV2>(OspfArea&, std::vector<OspfRouteChange>&);
template void OspfProcess::reoriginateSummaries<PolicyV3>(OspfArea&, std::vector<OspfRouteChange>&);
}
