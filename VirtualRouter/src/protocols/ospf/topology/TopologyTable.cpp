// OspfTopologyTable.cpp

#include <algorithm>

#include "TopologyTable.h"
#include "ospf/spf/SpfTypes.hpp"

namespace OSPF
{
static void dedupe(std::vector<OspfNextHop>& nh)
{
    std::sort(nh.begin(), nh.end(), [](auto& a, auto& b) {
        return std::tie(a.interfaceId, a.nextHop)
             < std::tie(b.interfaceId, b.nextHop);
    });

    nh.erase(std::unique(nh.begin(), nh.end()), nh.end());
}

static std::vector<OspfNextHop> extractNextHops(const SptNode& node)
{
    std::vector<OspfNextHop> out;
    out.reserve(node.parents.size());

    for (const ParentRef& p : node.parents)
    {
        if (p.firstHopIfid == 0)
            continue;

        out.push_back(OspfNextHop{
            .interfaceId = p.firstHopIfid,
            .nextHop = IPAddress{}
        });
    }

    dedupe(out);
    return out;
}

bool TopologyTable::consumeSpfResult(uint32_t area, const SpfResult& spf)
{
    std::vector<OspfRouter> canidates;

    for (const auto& [v, node] : spf.nodes)
    {
        if (!node.confirmed)
            continue;
        if (v.type != VertexType::ROUTER)
            continue;
        if (v == spf.root)
            continue;

        const uint32_t rid = static_cast<uint32_t>(v.id);
        const uint64_t cost = node.dist;

        std::vector<OspfNextHop> nh = extractNextHops(node);

        if (nh.empty())
            continue;

        canidates.emplace_back(rid, cost, std::move(nh));
    }

    return mergeCanidates(area, canidates, Type::INTRA);
}

bool TopologyTable::updateAreaAsbrs(uint32_t area, const std::vector<OspfRouter>& asbrs)
{
    return mergeCanidates(area, asbrs, Type::INTER);
}

bool TopologyTable::updateAreaAsbr(uint32_t area, const OspfRouter& asbr, bool remove)
{
    if (remove)
    {
        areaReach[area].erase(ReachEntry{Type::INTER, asbr.rid});
        return reach.erase(asbr.rid);
    }

    auto status = mergeCanidate(asbr);
    bool change = status.has_value();
    if (change && status.value())
    {
        areaReach[area].emplace(Type::INTER, asbr.rid);
    }
    return change;
}

const RouterReach* TopologyTable::lookup(uint32_t rid) const
{
    auto it = reach.find(rid);
    if (it == reach.end())
        return nullptr;
    return &it->second;
}

uint32_t TopologyTable::lookupDistance(uint32_t rid) const
{
    auto it = reach.find(rid);
    if (it == reach.end())
        return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(it->second.cost);
}

void TopologyTable::clear()
{
    reach.clear();
}

std::optional<bool> TopologyTable::mergeCanidate(const OspfRouter& canidate)
{
    auto it = reach.find(canidate.rid);

    if (it == reach.end())
    {
        if (!canidate.nextHops.empty())
        {
            reach.emplace(canidate.rid, RouterReach{canidate.cost, std::move(canidate.nextHops)});
            return true;
        }
        return std::nullopt;
    }

    RouterReach& cur = it->second;

    if (canidate.cost < cur.cost)
    {
        cur.cost = canidate.cost;
        cur.nextHops = std::move(canidate.nextHops);
        return false;
    }

    bool change = cur.nextHops.size() != canidate.nextHops.size();

    if (!change)
    {
        auto curNh = cur.nextHops;
        auto newNh = canidate.nextHops;

        std::sort(curNh.begin(), curNh.end());
        std::sort(newNh.begin(), newNh.end());

        change = curNh != newNh;
    }

    cur.nextHops = canidate.nextHops;
    dedupe(cur.nextHops);

    if (change) return false;
    return std::nullopt;
}

bool TopologyTable::mergeCanidates(uint32_t area, const std::vector<OspfRouter>& canidates, Type type)
{
    bool change = false;

    auto& areaList = areaReach[area];

    std::unordered_set<uint32_t> untouched;
    for (const auto& entry : areaList)
        if (entry.type == type) untouched.emplace(entry.rid);

    for (auto& canidate : canidates)
    {
        untouched.erase(canidate.rid);
        auto status = mergeCanidate(canidate);
        if (status.has_value())
        {
            change = true;
            // Check if added
            if (status.value()) areaList.emplace(type, canidate.rid);
        }
    }

    if (!untouched.empty()) change = true;
    for (const auto& rid : untouched)
        areaList.erase(ReachEntry{type, rid});

    return change;
}
}
