// GlobalAggregator.cpp

#include "GlobalAggregator.h"
#include "InterfaceManager.h"
#include "Eigrp.h"
#include "EigrpTopology.h"
#include <EigrpInterface.h>
#include <Functions.h>
#include <RouteAggregator.h>

namespace Eigrp
{
GlobalAggregator::GlobalAggregator(Eigrp& base) : base(base) {}

void GlobalAggregator::addSummary(TopologyEntry& top)
{
    std::shared_lock<std::shared_mutex> lock(base.getIfaceMgr().interfaceMutex);
    for (auto& [id, iface] : base.getIfaceMgr().eigrpInterfaceList)
    {
        if (auto* sum = iface.getAggregator().isSummarized(top.prefix); sum)
        {
            top.suppression[id].summaries.insert(sum);
            sum->summarizedRoutes.insert(top.prefix);
        }
    }
}

void GlobalAggregator::updateSummary(TopologyEntry& top)
{
    if (top.suppression.empty()) return;
    std::shared_lock<std::shared_mutex> lock(base.getIfaceMgr().interfaceMutex);
    auto& interfaces = base.getIfaceMgr().eigrpInterfaceList;
    auto bestRt = top.successors.begin();
    if (bestRt == top.successors.end()) return;
    for (auto& [id, info] : top.suppression)
    {
        if (auto ifaceIt = interfaces.find(id); ifaceIt != interfaces.end())
            for (auto sum : info.summaries)
                ifaceIt->second.getAggregator().updateSummaryRoute(*sum);
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

    if (enable)
    {
        std::set<IPPrefix> classfulGroups;

        for (auto entry : base.getTopology().entries())
        {
            if (entry.second->successors.empty())
                continue;
            IPAddress major;
            uint8_t mask = Functions::findClassfullNetworkAndMask(major.raw, entry.first.addr);
            classfulGroups.insert({major, mask});
        }

        std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
        for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
        {
            iface.getAggregator().installSummaries(classfulGroups, true);
        }
    }
    else
    {
        std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
        for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
        {
            iface.getAggregator().clearAutoSummaries();
        }
    }
}

void GlobalAggregator::recomputeAutoSummaries()
{
    auto& ifmgr = base.getIfaceMgr();
    std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
    for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
    {
        iface.getAggregator().updateAllSummaryRoutes(true);
    }
}
}
