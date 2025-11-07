// GlobalAggregator.cpp

#include "GlobalAggregator.h"
#include "InterfaceManager.h"
#include "EigrpCore.h"
#include "EigrpTopology.h"
#include <EigrpInterface.h>
#include <RouteAggregator.h>

namespace Eigrp
{
GlobalAggregator::GlobalAggregator(Eigrp& base) : base(base) {}

void GlobalAggregator::addSummary(TopologyEntry& top)
{
    std::shared_lock<std::shared_mutex> lock(base.getIfaceMgr().interfaceMutex);
    for (auto& [id, iface] : base.getIfaceMgr().eigrpInterfaceList)
    {
        if (auto* sum = iface->getAggregator().isSummarized(top.prefix); sum)
        {
            top.summaries[id] = sum;
            sum->summarizedRoutes.insert(top.prefix);
        }
    }
}

void GlobalAggregator::updateSummary(TopologyEntry& top)
{
    std::shared_lock<std::shared_mutex> lock(base.getIfaceMgr().interfaceMutex);
    auto interfaces = base.getIfaceMgr().eigrpInterfaceList;
    for (auto& [id, sum] : top.summaries)
    {
        if (auto ifaceIt = interfaces.find(id); ifaceIt != interfaces.end())
            ifaceIt->second->getAggregator().updateSummaryRoute(*sum);
    }
}

void GlobalAggregator::enableAutoSummary(bool enable)
{
    if (base.getAF() != AddressFamily::IPv4) return; // Only supported for IPv4

    auto& configs = base.getGlobalConfigMgr();
    bool current = configs.isAutoSummarized();
    if (enable == current) return; // No change

    configs.setAutoSummary(enable);

    // Lock interface for duration
    auto& ifmgr = base.getIfaceMgr();
    std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);

    if (enable)
    {
        std::map<std::pair<IPAddress, uint8_t>, std::vector<const RouteInfo*>> classfulGroups;

        for (auto entry : base.getTopology().entries())
        {
            if ()
        }
    }



    
    if (enable)
    {
        std::map<std::pair<IPAddress, uint8_t>, std::vector<RoutingTable::Eigrp*>> classfulGroups;

        // Process all existing EIGRP routes and group by classical networks
        for (RoutingTable::Eigrp* route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
        {
            if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                continue;
            IPAddress major;
            uint8_t mask = Functions::findClassfullNetworkAndMask(major.raw, route->network.raw);
            classfulGroups[{major, mask}].push_back(route);
        }

        // Only summarize when there are 2+ subnets in a classful group
        for (const auto& [majorNet, routes] : classfulGroups)
        {
            if (routes.size() < 2) continue;

            // For each interface, check if summary is missing
            for (const auto& [_, iface] : eigrpInterfaceList)
            {
                if (!iface->isRouteSummarized(majorNet.first.raw, majorNet.second))
                {
                    iface->addSummaryRoute(majorNet.first.raw, majorNet.second, true);
                }
            }
        }
    }
    else 
    {
        // Disable auto-summarization on all interfaces
        for (const auto& [_, iface] : eigrpInterfaceList)
        {
            iface->removeAllAutoSummaries();
        }
    }
}

void EigrpConfig::recomputeAutoSummaries()
{
    if (!configs.autoSummarizationEnabled.load(std::memory_order_relaxed) || addressFamily != AddressFamily::IPv4)
        return;

    // Step 1: Group all connected EIGRP routes by classful major network
    std::map<std::pair<IPAddress, uint8_t>, std::vector<RoutingTable::Eigrp*>> grouped;
    for (RoutingTable::Eigrp* route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
    {
        if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
            continue;
        IPAddress major;
        uint8_t mask = Functions::findClassfullNetworkAndMask(major.raw, route->network.raw);
        grouped[{major, mask}].push_back(route);
    }

    // Step 2: Loop through all known major networks
    std::shared_lock<std::shared_mutex> ifaceLock(interfaceMutex);

    for (const auto& [majorNet, routes] : grouped)
    {
        // Compute best metric among components
        uint32_t minBandwidth = std::numeric_limits<uint32_t>::max();
        uint32_t minDelay = std::numeric_limits<uint32_t>::max();

        for (const auto* r : routes)
        {
            if (r->bandwidth < minBandwidth)
                minBandwidth = r->bandwidth;
            if (r->delay < minDelay)
                minDelay = r->delay;
        }

        // If less than 2, treat as no summary opportunity
        if (routes.size() < 2)
        {
            for (const auto& [_, iface] : eigrpInterfaceList)
            {
                if (iface->isRouteSummarized(majorNet.first.raw, majorNet.second))
                    iface->removeSummaryRoute(majorNet.first, majorNet.second);
            }
            continue;
        }

        // Step 3: Ensure summary exists on each interface and has correct metric
        for (const auto& [_, iface] : eigrpInterfaceList)
        {
            // Check if summary is already present
            bool found = false;
            {
                std::shared_lock<std::shared_mutex> lock(iface->configs->configsMutex);
                for (const auto& sr : iface->configs->summaryRoutes)
                {
                    if (sr.isAuto &&
                        sr.summary->network == majorNet.first &&
                        sr.summary->mask == majorNet.second)
                    {
                        found = true;

                        // Check if the metric needs updating
                        if (sr.summary->bandwidth != minBandwidth || sr.summary->delay != minDelay)
                        {
                            iface->removeSummaryRoute(majorNet.first, majorNet.second);
                            iface->addSummaryRoute(majorNet.first.raw, majorNet.second, true);
                        }

                        break;
                    }
                }
            }

            // Not found? Add new summary
            if (!found)
                iface->addSummaryRoute(majorNet.first.raw, majorNet.second, true);
        }
    }

    // Step 4: Clean up any summaries that no longer match anything
    for (const auto& [_, iface] : eigrpInterfaceList)
    {
        std::unique_lock<std::shared_mutex> lock(iface->configs->configsMutex);
        for (auto it = iface->configs->summaryRoutes.begin(); it != iface->configs->summaryRoutes.end(); )
        {
            if (!it->isAuto)
            {
                ++it;
                continue;
            }

            // Does this still match 2+ connected routes?
            int matchCount = 0;
            for (const auto* route : routingInstance->routingTable.getAllConnectedEigrpRoutes(addressFamily, asNumber))
            {
                if (Functions::isSubnetOf(route->network.raw, route->mask, it->summary->network.raw, it->summary->mask, addressFamily))
                {
                    matchCount++;
                    if (matchCount >= 2)
                        break;
                }
            }

            if (matchCount < 2)
            {
                iface->removeSummaryRoute(it->summary->network, it->summary->mask);
                it = iface->configs->summaryRoutes.erase(it); // erase here because we’re in the loop
            }
            else
            {
                ++it;
            }
        }
    }
}
}
