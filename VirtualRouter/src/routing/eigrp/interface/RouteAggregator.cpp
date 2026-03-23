// EigrpInterfaceSummary.cpp

#include "RouteAggregator.h"
#include "EigrpInterface.h"
#include "interface/configs/InterfaceConfigs.h"
#include "eigrp/core/Eigrp.h"

namespace routing::eigrp
{
RouteAggregator::RouteAggregator(EigrpInterface& iface)
    : iface(iface) {}

RouteAggregator::~RouteAggregator()
{
    for (const auto& [prefix, sr] : summaryRoutes)
    {
        if (!sr.isAuto)
            iface.pendingSummaryRoutes.emplace_back(prefix);
    }
    clearAutoSummaries();
    summaryRoutes.clear();
}

void RouteAggregator::clearAutoSummaries()
{
    std::vector<types::IPPrefix> withdraws;
    for (auto route : summaryRoutes)
        if (route.second.isAuto)
            withdraws.push_back(route.first);
    for (auto pref : withdraws)
        withdrawSummary(pref);
}

SummaryRoute* RouteAggregator::isSummarized(const types::IPPrefix& prefix)
{
    for (auto& sum : summaryRoutes)
    {
        if (sum.first.prefixLength < prefix.prefixLength &&
            sum.first.contains(types::IPAddress(prefix.addr, prefix.prefixLength)))
        {
            return &sum.second;
        }
    }
    return nullptr;
}

void RouteAggregator::updateSummaryRoutes(std::vector<SummaryRoute*>& ss)
{
    std::vector<TopologyEntry*> changedRoutes;

    for (auto& s : ss)
    {
        auto [valid, changed] = calculateSummary(*s);

        if (changed)
        {
            auto& entries = iface.getTopController().getTopologies();
            if (!valid)
            {
                for (auto& r : s->summarizedRoutes)
                {
                    if (auto eit = entries.find(r); eit != entries.end())
                    {
                        eit->second.suppression[iface.interfaceKey].summaries.erase(s);
                        changedRoutes.push_back(&eit->second);
                    }
                }
                s->summarizedRoutes.clear();
            }
            else
            {
                types::IPPrefix prefix = s->summaryEntry->prefix;
                for (auto& entry : iface.getTopController().getTopologies())
                {
                    if (entry.second.prefix.prefixLength >= prefix.prefixLength &&
                        prefix.contains(types::IPAddress(entry.second.prefix.addr, entry.second.prefix.prefixLength)))
                    {
                        entry.second.suppression[iface.interfaceKey].summaries.insert(s);
                        s->summarizedRoutes.insert(entry.second.prefix);
                        changedRoutes.push_back(&entry.second);
                    }
                }
            }
            changedRoutes.push_back(s->summaryEntry);
        }
    }
    if (!changedRoutes.empty())
        iface.getTopController().refreshSuppression(changedRoutes);
}

void RouteAggregator::updateSummaryRoute(SummaryRoute& s)
{
    auto [valid, changed] = calculateSummary(s);
    std::vector<TopologyEntry*> changedRoutes;

    if (changed)
    {
        auto& entries = iface.getTopController().getTopologies();
        if (!valid)
        {
            iface.getTopController().markRouteUnreachable(*s.summaryRoute, iface.ifaceAddress, *s.summaryEntry);
            for (auto& r : s.summarizedRoutes)
            {
                if (auto eit = entries.find(r); eit != entries.end())
                {
                    eit->second.suppression[iface.interfaceKey].summaries.erase(&s);
                    changedRoutes.push_back(&eit->second);
                }
            }
            s.summarizedRoutes.clear();
        }
        else
        {
            types::IPPrefix prefix = s.summaryEntry->prefix;
            for (auto& entry : iface.getTopController().getTopologies())
            {
                if (entry.second.prefix.prefixLength >= prefix.prefixLength &&
                    prefix.contains(types::IPAddress(entry.second.prefix.addr, entry.second.prefix.prefixLength)))
                {
                    entry.second.suppression[iface.interfaceKey].summaries.insert(&s);
                    s.summarizedRoutes.insert(entry.second.prefix);
                    changedRoutes.push_back(&entry.second);
                }
            }
        }
        changedRoutes.push_back(s.summaryEntry);
    }

    if (!changedRoutes.empty())
        iface.getTopController().refreshSuppression(changedRoutes);
}

std::pair<bool, bool> RouteAggregator::calculateSummary(SummaryRoute& s)
{
    ReceivedRoute& r = s.summaryRoute->routeInfo;
    uint64_t oldFD = r.feasibleDistance;
    auto& base = iface.getBase();
    auto& topology = iface.getTopController();
    auto& entries = topology.getTopologies();

    const ReceivedRoute* bestRoute = nullptr;

    for (auto& [_, entry] : entries)
    {
        if (s.summarizedRoutes.contains(entry.prefix))
        {
            auto it = entry.routesBySource.find(entry.bestNeighbor);
            if (it != entry.routesBySource.end())
            {
                const auto& rt = it->second.routeInfo;
                if (!bestRoute || bestRoute->feasibleDistance > rt.feasibleDistance)
                    bestRoute = &rt;
            }
        }
    }

    types::AddressFamily af = iface.getBase().getAF();

    auto& ifCfg = iface.getIfaceCfg();
    if (af == types::AddressFamily::IPv4)
    {
        r.nextHop.setV4(ifCfg.ipv4.getPrimaryAddress().addr);
    }
    else
    {
        r.nextHop.setV6(ifCfg.ipv6.getLocalAddress().addr);
    }

    r.originInterface = iface.interfaceKey;
    r.reportedDistance = 0;
    r.hopCount = 0;
    r.tag = 0;
    r.adminDistance = base.getGlobalConfigMgr().getAD();
    r.routeType = RouteType::SUMMARY;
    r.flags = 0;
    
    r.wide.isWide = true;
    r.wide.topology = 0;
    r.wide.afi = (base.getAF() == types::AddressFamily::IPv6) ? 2 : 1;
    r.wide.rid = base.routerID();
    r.wide.priority = 0;

    if (bestRoute)
    {
        r.bandwidth = bestRoute->bandwidth;
        r.delay = bestRoute->delay;
        r.mtu = bestRoute->mtu;
        r.reliability = bestRoute->reliability;
        r.load = bestRoute->load;
        r.feasibleDistance = bestRoute->feasibleDistance;
        r.reportedDistance = bestRoute->reportedDistance;
        return {true, r.feasibleDistance != oldFD };
    }
    else
    {
        r.feasibleDistance = std::numeric_limits<uint64_t>::max();
        return {false, false};
    }
}

void RouteAggregator::updateAllSummaryRoutes(bool isAuto)
{
    std::vector<SummaryRoute*> updatedRoutes;
    for (auto& [_, s] : summaryRoutes)
    {
        if (s.isAuto == isAuto)
            updatedRoutes.push_back(&s);
    }
    updateSummaryRoutes(updatedRoutes);
}

void RouteAggregator::installSummaries(const std::set<types::IPPrefix>& prefixes, bool isAuto)
{
    auto createSumRoute = [&](TopologyEntry& top, ReceivedRoute& r) -> RouteInfo* {
        auto it = top.routesBySource.emplace(iface.ifaceAddress, RouteInfo{r});
        return &it.first->second;
    };
    
    std::vector<SummaryRoute*> newSummaries;
    for (const auto& prefix : prefixes)
    {
        if (summaryRoutes.contains(prefix)) continue;
        auto sit = summaryRoutes.emplace(prefix, SummaryRoute{ .isAuto = isAuto });
        SummaryRoute& s = sit.first->second;

        ReceivedRoute r{};
        r.prefix = prefix;

        auto& top = iface.getTopController().ensure(prefix);
        s.summaryEntry = &top;
        s.summaryRoute = createSumRoute(top, r);

        newSummaries.push_back(&s);
    }

    // Add summary info
    updateSummaryRoutes(newSummaries);
}

void RouteAggregator::installSummary(const types::IPPrefix& prefix, bool isAuto)
{
    if (summaryRoutes.contains(prefix)) return;
    auto sit = summaryRoutes.emplace(prefix, SummaryRoute{ .isAuto = isAuto });
    SummaryRoute& s = sit.first->second;

    ReceivedRoute r{};
    r.prefix = prefix;

    auto& top = iface.getTopController().ensure(prefix);
    auto createSumRoute = [&](TopologyEntry& top) -> RouteInfo* {
        auto it = top.routesBySource.emplace(iface.ifaceAddress, RouteInfo{r});
        return &it.first->second;
    };
    s.summaryEntry = &top;
    s.summaryRoute = createSumRoute(top);

    // Add summary info
    updateSummaryRoute(s);
}

void RouteAggregator::withdrawSummary(const types::IPPrefix& prefix)
{
    auto it = summaryRoutes.find(prefix);
    if (it == summaryRoutes.end()) return;

    SummaryRoute& s = it->second;

    iface.getTopController().markRouteUnreachable(*s.summaryRoute, iface.ifaceAddress, *s.summaryEntry);

    updateSummaryRoute(s);
    summaryRoutes.erase(it);
}
} // namespace routing
