// OspfRoutingTable.cpp

#include "OspfRoutingTable.h"
#include <mutex>

namespace OSPF
{
const OspfRoute* OspfRib::lookup(const IPPrefix& prefix) const
{
    std::shared_lock<std::shared_mutex> lock(mutex);

    auto it = globalRoutes.find(prefix);
    return it != globalRoutes.end() ? &it->second : nullptr;
}

void OspfRib::replaceArea(uint32_t areaId, const std::vector<std::pair<IPPrefix, OspfPath>>& paths)
{
    std::unique_lock lock(mutex);

    std::unordered_set<IPPrefix> touched;

    auto idx = areaIndex.find(areaId);
    if (idx != areaIndex.end())
    {
        for (const auto& prefix : idx->second)
        {
            auto& vec = allPaths[prefix];
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [areaId](const OspfPath& p) { return p.area == areaId; }),
                vec.end());
            touched.insert(prefix);
        }
        areaIndex.erase(idx);
    }

    for (const auto& [prefix, path] : paths)
    {
        allPaths[prefix].push_back(path);
        areaIndex[areaId].insert(prefix);
        touched.insert(prefix);
    }

    for (const auto& prefix : touched)
        recomputeLocked(prefix);
}

void OspfRib::recomputeLocked(const IPPrefix& prefix)
{
    auto it = allPaths.find(prefix);
    if (it == allPaths.end() || it->second.empty())
    {
        globalRoutes.erase(prefix);
        return;
    }

    const auto& paths = it->second;
    const OspfPath* best = nullptr;

    for (const auto& p : paths)
    {
        if (!best || p.type < best->type || (p.type == best->type && p.cost < best->cost))
        {
            best = &p;
        }
    }

    OspfRoute route;
    route.prefix = prefix;
    route.type = best->type;
    route.cost = best->cost;

    for (const auto& p : paths)
    {
        if (p.type == route.type && p.cost == route.cost)
            route.paths.push_back(p);
    }

    globalRoutes[prefix] = std::move(route);
}
}
