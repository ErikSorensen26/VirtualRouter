// GlobalAggregator.cpp

#include "GlobalAggregator.h"
#include "InterfaceManager.h"
#include "Eigrp.h"
#include "Topology.h"
#include "eigrp/interface/RouteAggregator.h"
#include "eigrp/interface/EigrpInterface.h"

namespace Eigrp
{
GlobalAggregator::GlobalAggregator(Eigrp& base) : base(base) {}

void GlobalAggregator::addSummary(TopologyEntry& top)
{
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
            classfulGroups.emplace(entry.first.v4(), entry.first.getDefaultMask());
        }

        for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
        {
            iface.getAggregator().installSummaries(classfulGroups, true);
        }
    }
    else
    {
        for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
        {
            iface.getAggregator().clearAutoSummaries();
        }
    }
}

void GlobalAggregator::recomputeAutoSummaries()
{
    auto& ifmgr = base.getIfaceMgr();
    for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
    {
        iface.getAggregator().updateAllSummaryRoutes(true);
    }
}
}
