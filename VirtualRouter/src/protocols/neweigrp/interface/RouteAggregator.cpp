// EigrpInterfaceSummary.cpp

#include "RouteAggregator.h"
#include <mutex>
#include "EigrpInterface.h"
#include <EigrpCore.h>
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
        if (sum.first.prefixLength > prefix.prefixLength &&
            Functions::compareNetworkWithIp(sum.first.addr, prefix.addr, sum.first.prefixLength, iface.getBase().getAF()))
        {
            return &sum.second;
        }
    }
    return nullptr;
}

void RouteAggregator::updateSummaryRoute(SummaryRoute& r, ReceivedRoute& route)
{
    if (route.feasibleDistance < r.summaryRoute.routeInfo.feasibleDistance)
    {
        
    }
}

bool RouteAggregator::calculateSummary(SummaryRoute& s)
{
    ReceivedRoute& r = s.summaryRoute.routeInfo;
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
    r.routeType = RoutingTable::Eigrp::RouteType::SUMMARY;
    
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
        return true;
    }
    else
    {
        r.feasibleDistance = std::numeric_limits<uint64_t>::max();
        return false;
    }
}

void RouteAggregator::updateAllSummaryRoutes()
{
    for (auto& [_, s] : summaryRoutes)
    {
        updateSummaryRoute(s);
    }
}

void RouteAggregator::installSummary(const IPPrefix& prefix, bool isAuto)
{
    std::lock_guard<std::mutex> lock(mtx);
    ReceivedRoute r{};
    auto it = summaryRoutes.emplace(prefix, RouteInfo{r}, isAuto, false, std::set<IPPrefix>{});
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
            sum.second.supressed = true;
            s.summarizedRoutes.insert(sum.first);
        }
        else if (sum.first.prefixLength < prefix.prefixLength &&
            Functions::compareNetworkWithIp(sum.first.addr, prefix.addr, sum.first.prefixLength, iface.getBase().getAF()))
        {
            s.supressed = true;
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

    std::vector<TopologyEntry*> supressedRoutes;
    std::vector<const RouteInfo*> supressedSummaries;

    auto& entries = iface.getTopController().getTopologies();
    for (auto r : it->second.summarizedRoutes)
    {
        if (auto sit = summaryRoutes.find(r); sit != summaryRoutes.end())
        {
            sit->second.supressed = false;
            supressedSummaries.push_back(&sit->second.summaryRoute);
        }
        else if (auto eit = entries.find(r); eit != entries.end())
        {
            eit->second->summaries.erase(iface.interfaceKey);
            supressedRoutes.push_back(eit->second);
        }
    }

    iface.getBase().routeManager.synchronizeRoutes(supressedRoutes, {prefix}, supressedSummaries);
}
}
