// OspfTopology.cpp

#include "OspfTopology.h"
#include <OspfRegistry.hpp>
#include <OspfProcess.h>
#include <OspfRoutingTable.h>
#include <OspfRouteManager.h>
#include <VirtualRouter.h>
#include <Global.h>
#include <OspfRegistry.hpp>

namespace OSPF
{
Topology::Topology(OspfProcess& p, uint8_t t, AddressFamily f)
    : tid(t), process(p), af(f), rib(*this),
    configs([&p, &t]() {
        if (p.isV3)
        {
            return p.configs->get<Config::Ospf::BASE>().get();
        }
        else
        {
            auto& base = p.configs->get<Config::Ospf::BASE>().get();
            auto key = Config::generateOspfTopologyKey(base.getKey(), t);
            auto conf = p.routingInstance->global.registry.create<Config::OspfTopologyRegistry>(key, base.get());
            p.routingInstance->global.registry.ensure(conf->get<Config::OspfTopology::BASE>(), key);
            return conf;
        }
    }()),
    baseConfigs(configs->get<Config::OspfTopology::BASE>().local())
{}

OspfArea* Topology::getArea(uint32_t areaId)
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    auto it = areas.find(areaId);
    if (it == areas.end()) return nullptr;
    return &it->second;
}
    
OspfArea& Topology::insureArea(uint32_t areaId)
{
    std::unique_lock<std::shared_mutex> lock(areaMu);
    if (!areas.contains(areaId))
    {
        auto a = areas.try_emplace(areaId, process, areaId); // TODO: maybe add pmr
        if (a.second) areaSize.store(areas.size(), std::memory_order_release);
        isABR.store(areas.size() > 1 && areas.contains(0), std::memory_order_release);
    }
    return areas.at(areaId);
}

template <typename Policy>
void Topology::distributeExternalLsa(uint32_t areaId, const IncomingLsaContext& ctx, LsaBody& body)
{
    {
        std::shared_lock<std::shared_mutex> lock(areaMu);
        for (auto& [id, area] : areas)
        {
            if (id != areaId)
            {
                LsaBody bodyCpy = body;
                area.processExternalLsa(ctx, body);
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
            
        if (!seq.has_value() || seq.value() < ctx.header.sequence) return;

        auto& rec = externalDb[ctx.key];

        rec.first = ctx.header;
        rec.second = body;

        result = RouteManager::deriveExternalRoute<Policy>(*this, ctx.key, rec);
    }
    rib.replaceExternal(result);
}

template<typename Policy>
void Topology::flood()
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    for (auto& [_, area] : areas)
        area.flood<Policy>();
}

template <typename Policy>
void Topology::reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList)
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

            key.advertisingRouter = process.getRouterId();
            key.linkStateId = readU32(path.prefix.addr);
            key.lsaType = OSPFV2_LSA_SUM_NET;
        }
        else
        {
            network.prefix = path.prefix;
            network.metric = static_cast<uint32_t>(path.cost);
            network.options = path.options;

            key.advertisingRouter = process.getRouterId();
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

template void Topology::distributeExternalLsa<PolicyV2>(uint32_t, const IncomingLsaContext&, LsaBody&);
template void Topology::distributeExternalLsa<PolicyV3>(uint32_t, const IncomingLsaContext&, LsaBody&);

template void Topology::flood<PolicyV2>();
template void Topology::flood<PolicyV3>();

template void Topology::reoriginateSummaries<PolicyV2>(OspfArea&, std::vector<OspfRouteChange>&);
template void Topology::reoriginateSummaries<PolicyV3>(OspfArea&, std::vector<OspfRouteChange>&);
}
