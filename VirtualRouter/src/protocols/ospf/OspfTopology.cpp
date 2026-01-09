// OspfTopology.cpp

#include "OspfTopology.h"
#include <OspfProcess.h>
#include <OspfRoutingTable.h>

namespace OSPF
{
Topology::Topology(OspfProcess& p, uint8_t t)
    : tid(t), process(p)
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

OspfArea::Result Topology::processExternalLsa(uint32_t areaId, const IncomingLsaContext& ctx, LsaBody& body)
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    OspfArea::Result result;
    for (auto& [id, area] : areas)
    {
        if (id == areaId)
            result = area.processLsa(ctx, body);
        else
            area.processLsa(ctx, body);
    }
    return result;
}

void Topology::flood()
{
    std::shared_lock<std::shared_mutex> lock(areaMu);
    for (auto& [_, area] : areas)
        area.flood();
}

template <typename SummaryNetwork, typename SummaryRouter>
void Topology::reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList)
{
    if (!isABR.load(std::memory_order_relaxed) || areaSize.load(std::memory_order_relaxed) == 1) return;

    std::vector<std::pair<LsaKey, SummaryNetwork>> networks;

    for (const auto& path : pathList)
    {
        auto& net = networks.emplace_back(LsaKey{}, SummaryNetwork{});

        LsaKey& key = net.first;
        SummaryNetwork& network = net.second;

        if constexpr (std::is_same_v<std::remove_cv_t<SummaryNetwork>, SummaryNetworkLsa>)
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
            for (const auto& network : networks)
            {
                LsaHeader hdr = {
                    .age = 0,
                    .
                }
                IncomingLsaContext
                {

                }
                a.processLsa()
            }
        }

        if (sourceAreaId == 0)
        {
            for (auto& [id, area] : areas)
            {
                if (id == 0) continue;
                auto& areaCfgs = area.getConfigs();
                if (areaCfgs.nssa.enabled.load(std::memory_order_relaxed) || areaCfgs.stub.enabled.load(std::memory_order_relaxed))
                    continue;

            }
        }

        for (const auto& )
    }
}

template void Topology::reoriginateSummaries<SummaryNetworkLsa, SummaryRouterLsa>(OspfArea&, std::vector<OspfRouteChange>&);
template void Topology::reoriginateSummaries<InterAreaPrefixLsa, InterAreaRouterLsa>(OspfArea&, std::vector<OspfRouteChange>&);
}
