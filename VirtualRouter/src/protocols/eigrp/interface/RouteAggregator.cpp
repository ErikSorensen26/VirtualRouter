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
            withdraws.push_back(route.second.summaryRoute.routeInfo.prefix);
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
    std::vector<IPPrefix> withdraws;
    std::vector<TopologyEntry*> suppressedRoutes;
    std::vector<const RouteInfo*> changedSummaries;

    for (auto& s : ss)
    {
        auto [valid, changed] = calculateSummary(*s);

        if (!valid || s->summaryRoute.routeInfo.feasibleDistance == std::numeric_limits<uint64_t>::max())
        {
            if (!s->suppressed)
            {
                auto& entries = iface.getTopController().getTopologies();
                for (auto r : s->summarizedRoutes)
                {
                    if (auto sit = summaryRoutes.find(r); sit != summaryRoutes.end())
                    {
                        sit->second.suppressed = false;
                        changedSummaries.push_back(&sit->second.summaryRoute);
                    }
                    else if (auto eit = entries.find(r); eit != entries.end())
                    {
                        eit->second->summaries.erase(iface.interfaceKey);
                        suppressedRoutes.push_back(eit->second);
                    }
                }

                withdraws.push_back(s->summaryRoute.routeInfo.prefix);
            }
            else if (changed)
            {
                changedSummaries.push_back(&s->summaryRoute);
            }

            s->summarizedRoutes.clear();
        }
    }
    if (!suppressedRoutes.empty() || !withdraws.empty() || !changedSummaries.empty())
        iface.getBase().routeManager.synchronizeRoutes(suppressedRoutes, withdraws, changedSummaries);
}

void RouteAggregator::updateSummaryRoute(SummaryRoute& s)
{
    auto [valid, changed] = calculateSummary(s);
    std::vector<IPPrefix> withdraws;
    std::vector<TopologyEntry*> suppressedRoutes;
    std::vector<const RouteInfo*> changedRoutes;

    if (!valid || s.summaryRoute.routeInfo.feasibleDistance == std::numeric_limits<uint64_t>::max())
    {
        if (!s.suppressed)
        {
            auto& entries = iface.getTopController().getTopologies();
            for (auto r : s.summarizedRoutes)
            {
                if (auto sit = summaryRoutes.find(r); sit != summaryRoutes.end())
                {
                    sit->second.suppressed = false;
                    changedRoutes.push_back(&sit->second.summaryRoute);
                }
                else if (auto eit = entries.find(r); eit != entries.end())
                {
                    eit->second->summaries.erase(iface.interfaceKey);
                    suppressedRoutes.push_back(eit->second);
                }
            }

            withdraws.push_back(s.summaryRoute.routeInfo.prefix);
        }

        s.summarizedRoutes.clear();
    }
    else if (changed)
    {
        changedRoutes.push_back(&s.summaryRoute);
    }

    if (!withdraws.empty() || !suppressedRoutes.empty() || !changedRoutes.empty())
        iface.getBase().routeManager.synchronizeRoutes(suppressedRoutes, {withdraws}, changedRoutes);
}

std::pair<bool, bool> RouteAggregator::calculateSummary(SummaryRoute& s)
{
    ReceivedRoute& r = s.summaryRoute.routeInfo;
    uint64_t oldFD = r.feasibleDistance;
    auto& base = iface.getBase();
    auto& topology = iface.getTopController();
    auto& entries = topology.getTopologies();

    const ReceivedRoute* bestRoute = nullptr;

    for (auto& [_, entry] : entries)
    {
        if (s.summarizedRoutes.contains(entry->prefix))
        {
            auto it = entry->routesByNeighbor.find(entry->bestNeighbor);
            if (it != entry->routesByNeighbor.end())
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
    std::vector<SummaryRoute*> newSummaries;

    for (const auto& prefix : prefixes)
    {
        ReceivedRoute r{};
        auto it = summaryRoutes.emplace(prefix, SummaryRoute{RouteInfo{r}, isAuto, false, std::set<IPPrefix>{}});
        auto& s = it.first->second;

        for (auto& entry : iface.getTopController().getTopologies())
        {
            if (entry.second->prefix.prefixLength >= prefix.prefixLength &&
                Functions::compareNetworkWithIp(prefix.addr, entry.second->prefix.addr, prefix.prefixLength, iface.getBase().getAF()))
            {
                entry.second->summaries[iface.interfaceKey] = &s;
                s.summarizedRoutes.insert(entry.second->prefix);
            }
        }
        for (auto& sum : summaryRoutes)
        {
            if (sum.first.prefixLength >= prefix.prefixLength &&
                Functions::compareNetworkWithIp(prefix.addr, sum.first.addr, prefix.prefixLength, iface.getBase().getAF()))
            {
                sum.second.suppressed = true;
                s.summarizedRoutes.insert(sum.first);
            }
            else if (sum.first.prefixLength < prefix.prefixLength &&
                Functions::compareNetworkWithIp(sum.first.addr, prefix.addr, sum.first.prefixLength, iface.getBase().getAF()))
            {
                s.suppressed = true;
                sum.second.summarizedRoutes.insert(prefix);
            }
        }

        newSummaries.push_back(&s);
    }

    // Add summary info
    updateSummaryRoutes(newSummaries);
}

void RouteAggregator::installSummary(const IPPrefix& prefix, bool isAuto)
{
    std::lock_guard<std::mutex> lock(mtx);
    ReceivedRoute r{};
    r.prefix = prefix;
    auto it = summaryRoutes.emplace(prefix, SummaryRoute{RouteInfo{r}, isAuto, false, std::set<IPPrefix>{}});
    auto& s = it.first->second;

    for (auto& entry : iface.getTopController().getTopologies())
    {
        if (entry.second->prefix.prefixLength >= prefix.prefixLength &&
            Functions::compareNetworkWithIp(prefix.addr, entry.second->prefix.addr, prefix.prefixLength, iface.getBase().getAF()))
        {
            entry.second->summaries[iface.interfaceKey] = &s;
            s.summarizedRoutes.insert(entry.second->prefix);
        }
    }
    for (auto& sum : summaryRoutes)
    {
        if (sum.first.prefixLength >= prefix.prefixLength &&
            Functions::compareNetworkWithIp(prefix.addr, sum.first.addr, prefix.prefixLength, iface.getBase().getAF()))
        {
            sum.second.suppressed = true;
            s.summarizedRoutes.insert(sum.first);
        }
        else if (sum.first.prefixLength < prefix.prefixLength &&
            Functions::compareNetworkWithIp(sum.first.addr, prefix.addr, sum.first.prefixLength, iface.getBase().getAF()))
        {
            s.suppressed = true;
            sum.second.summarizedRoutes.insert(prefix);
        }
    }

    // Add summary info
    updateSummaryRoute(s);
}

void RouteAggregator::withdrawSummary(const IPPrefix& prefix)
{
    std::lock_guard<std::mutex> lock(mtx);
    
    auto it = summaryRoutes.find(prefix);
    if (it == summaryRoutes.end()) return;

    std::vector<TopologyEntry*> suppressedRoutes;
    std::vector<const RouteInfo*> suppressedSummaries;

    auto& entries = iface.getTopController().getTopologies();
    for (auto r : it->second.summarizedRoutes)
    {
        if (auto sit = summaryRoutes.find(r); sit != summaryRoutes.end())
        {
            sit->second.suppressed = false;
            suppressedSummaries.push_back(&sit->second.summaryRoute);
        }
        else if (auto eit = entries.find(r); eit != entries.end())
        {
            eit->second->summaries.erase(iface.interfaceKey);
            suppressedRoutes.push_back(eit->second);
        }
    }

    iface.getBase().routeManager.synchronizeRoutes(suppressedRoutes, {prefix}, suppressedSummaries);
    summaryRoutes.erase(it);
}
}
