// EigrpInterfaceSummary.cpp

#include "RouteAggregator.h"
#include <mutex>
#include "EigrpInterface.h"
#include <Functions.h>
#include <Eigrp.h>
#include <InterfaceConfigs.h>

namespace Eigrp
{
RouteAggregator::RouteAggregator(EigrpInterface& iface)
    : iface(iface) {}

RouteAggregator::~RouteAggregator()
{
    std::shared_lock<std::shared_mutex> lock(iface.configs->configsMutex);
    for (const auto& [prefix, sr] : summaryRoutes)
    {
        if (!sr.isAuto)
            iface.configs->pendingSummaryRoutes.emplace_back(prefix);
    }
    clearAutoSummaries();
    summaryRoutes.clear();
}

void RouteAggregator::clearAutoSummaries()
{
    std::lock_guard<std::mutex> lock(mtx);
    
    std::vector<IPPrefix> withdraws;
    for (auto route : summaryRoutes)
        if (route.second.isAuto)
            withdraws.push_back(route.first);
    for (auto pref : withdraws)
        withdrawSummary(pref);
}

SummaryRoute* RouteAggregator::isSummarized(const IPPrefix& prefix)
{
    for (auto& sum : summaryRoutes)
    {
        if (sum.first.prefixLength < prefix.prefixLength &&
            Functions::compareNetworkWithIp(sum.first.addr, prefix.addr, sum.first.prefixLength, iface.getBase().getAF()))
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
                        eit->second->suppression[iface.interfaceKey].summaries.erase(s);
                        changedRoutes.push_back(eit->second);
                    }
                }
                s->summarizedRoutes.clear();
            }
            else
            {
                IPPrefix prefix = s->summaryEntry->prefix;
                for (auto& entry : iface.getTopController().getTopologies())
                {
                    if (entry.second->prefix.prefixLength >= prefix.prefixLength &&
                        Functions::compareNetworkWithIp(prefix.addr, entry.second->prefix.addr, prefix.prefixLength, iface.getBase().getAF()))
                    {
                        entry.second->suppression[iface.interfaceKey].summaries.insert(s);
                        s->summarizedRoutes.insert(entry.second->prefix);
                        changedRoutes.push_back(entry.second);
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
                    eit->second->suppression[iface.interfaceKey].summaries.erase(&s);
                    changedRoutes.push_back(eit->second);
                }
            }
            s.summarizedRoutes.clear();
        }
        else
        {
            IPPrefix prefix = s.summaryEntry->prefix;
            for (auto& entry : iface.getTopController().getTopologies())
            {
                if (entry.second->prefix.prefixLength >= prefix.prefixLength &&
                    Functions::compareNetworkWithIp(prefix.addr, entry.second->prefix.addr, prefix.prefixLength, iface.getBase().getAF()))
                {
                    entry.second->suppression[iface.interfaceKey].summaries.insert(&s);
                    s.summarizedRoutes.insert(entry.second->prefix);
                    changedRoutes.push_back(entry.second);
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
        if (s.summarizedRoutes.contains(entry->prefix))
        {
            auto it = entry->routesBySource.find(entry->bestNeighbor);
            if (it != entry->routesBySource.end())
            {
                const auto& rt = it->second.routeInfo;
                if (!bestRoute || (bestRoute && bestRoute->feasibleDistance > rt.feasibleDistance))
                    bestRoute = &rt;
            }
        }
    }

    AddressFamily af = iface.getBase().getAF();

    auto& ifCfg = iface.getIfaceCfg();
    if (af == AddressFamily::IPv4)
    {
        ifCfg.ipv4.getAddress(r.nextHop.raw);
    }
    else
    {
        ifCfg.ipv6.getLocalAddress(r.nextHop.raw);
        r.nextHop.isV6 = true;
    }

    r.originInterface = iface.interfaceKey;
    r.reportedDistance = 0;
    r.hopCount = 0;
    r.tag = 0;
    r.adminDistance = base.getGlobalConfigMgr().getAD();
    r.routeType = RouteType::SUMMARY;
    
    r.wide.isWide = true;
    r.wide.topology = 0;
    r.wide.afi = (base.getAF() == AddressFamily::IPv6) ? 2 : 1;
    r.wide.rid = base.routerID();
    r.wide.priority = 0;
    r.wide.wideFlags = 0;

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

void RouteAggregator::installSummaries(const std::set<IPPrefix>& prefixes, bool isAuto)
{
    std::lock_guard<std::mutex> lock(mtx);

    auto createSumRoute = [&](TopologyEntry& top, ReceivedRoute& r) -> RouteInfo* {
        std::lock_guard<std::mutex>  toplock(top.entryMutex);
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

void RouteAggregator::installSummary(const IPPrefix& prefix, bool isAuto)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (summaryRoutes.contains(prefix)) return;
    auto sit = summaryRoutes.emplace(prefix, SummaryRoute{ .isAuto = isAuto });
    SummaryRoute& s = sit.first->second;

    ReceivedRoute r{};
    r.prefix = prefix;

    auto& top = iface.getTopController().ensure(prefix);
    auto createSumRoute = [&](TopologyEntry& top) -> RouteInfo* {
        std::lock_guard<std::mutex>  toplock(top.entryMutex);
        auto it = top.routesBySource.emplace(iface.ifaceAddress, RouteInfo{r});
        return &it.first->second;
    };
    s.summaryEntry = &top;
    s.summaryRoute = createSumRoute(top);

    // Add summary info
    updateSummaryRoute(s);
}

void RouteAggregator::withdrawSummary(const IPPrefix& prefix)
{
    auto it = summaryRoutes.find(prefix);
    if (it == summaryRoutes.end()) return;

    std::vector<TopologyEntry*> changedRoutes;
    SummaryRoute& s = it->second;

    iface.getTopController().markRouteUnreachable(*s.summaryRoute, iface.ifaceAddress, *s.summaryEntry);

    updateSummaryRoute(s);
    summaryRoutes.erase(it);
}
}
