// OspfTopology.cpp

#include "OspfTopology.h"

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
        areas.try_emplace(areaId, process, areaId); // TODO: maybe add pmr
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
}
