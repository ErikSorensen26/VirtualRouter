// RouteManagerUtility.cpp

#include "RouteManagerUtility.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/neighbor/Neighbor.h"

namespace routing::ospf
{
TopologyTable& RouteManagerUtility::getTopoTable(OspfProcess& proc)
{
    return proc.table;
}

const config::OspfRegistry& RouteManagerUtility::getProcessConfigs(OspfProcess& proc)
{
    return proc.configs;
}

LsdbTable& RouteManagerUtility::getLsdb(Area& area)
{
    return area.lsdb;
}

ExternalOriginator& RouteManagerUtility::getExtOriginator(OspfProcess& proc)
{
    return proc.externalOriginator;
}

const InterfaceManager& RouteManagerUtility::getIfaceMgr(OspfProcess& proc)
{
    return proc.ifaceMgr;
}

std::optional<OspfNextHop> RouteManagerUtility::resolveDirectNextHop(Area& area, const Vertex& v, const ParentRef& pref)
{
    // Get RID of destination router.
    uint32_t rid = v.type == VertexType::ROUTER
        ? static_cast<uint32_t>(v.id)
        : networkAdvRouter(v.id);

    auto& ifaceMgr = area.process.ifaceMgr;

    const OspfInterface* iface = ifaceMgr.getInterface({pref.firstHopIfid, area.areaId});
    if (!iface) return std::nullopt;

    const Neighbor* nbr = ifaceMgr.getNTable(*iface).lookup(rid);
    if (!nbr) return std::nullopt;

    // Return neighbors IP address.
    return OspfNextHop{pref.firstHopIfid, nbr->ipAddress};
}

void RouteManagerUtility::dedupeNextHops(std::vector<OspfNextHop>& hops)
{
    std::sort(hops.begin(), hops.end(),
        [](const OspfNextHop& a, const OspfNextHop& b)
        {
            if (a.interfaceId != b.interfaceId) return a.interfaceId < b.interfaceId;
            return a.nextHop < b.nextHop;
        });

    hops.erase(std::unique(hops.begin(), hops.end()), hops.end());
}

std::vector<OspfNextHop> RouteManagerUtility::computeNextHops(Area& area, const Vertex& v, const SpfResult& spf, NhCache& cache)
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

std::optional<std::pair<uint64_t, std::vector<OspfNextHop>>> RouteManagerUtility::resolveToAbr(Area& area, const SpfResult& spf, uint32_t abrRid, NhCache& nhCache)
{
    const Vertex abrV{ VertexType::ROUTER, abrRid };

    const auto nodeIt = spf.nodes.find(abrV);
    if (nodeIt == spf.nodes.end())
        return std::nullopt;

    auto nh = RouteManagerUtility::computeNextHops(area, abrV, spf, nhCache);
    if (nh.empty())
        return std::nullopt;

    const uint64_t dist = nodeIt->second.dist;
    return std::make_pair(dist, std::move(nh));
}

std::optional<RouterReach> RouteManagerUtility::resolveToAbrs(OspfProcess& proc, uint32_t abrRid)
{
    auto* res = RouteManagerUtility::getTopoTable(proc).lookup(abrRid);
    if (res) return *res;
    return std::nullopt;
}
}
