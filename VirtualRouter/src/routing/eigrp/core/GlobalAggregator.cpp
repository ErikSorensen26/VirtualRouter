// GlobalAggregator.cpp

#include "GlobalAggregator.h"
#include "InterfaceManager.h"
#include "Eigrp.h"
#include "Topology.h"
#include "eigrp/interface/RouteAggregator.h"
#include "eigrp/interface/EigrpInterface.h"

namespace routing::eigrp
{
GlobalAggregator::GlobalAggregator(Eigrp& base) : base(base) {}

void GlobalAggregator::addSummary(TopologyEntry& top)
{
    for (auto& [id, iface] : base.getIfaceMgr().eigrpInterfaceList)
    {
        if (auto* sum = iface.getAggregator().isSummarized(top.prefix); sum)
        {
            top.suppression[id.getId()].summaries.insert(sum);
            sum->summarizedRoutes.insert(top.prefix);
        }
    }
}

void GlobalAggregator::updateSummary(TopologyEntry& top)
{
    if (top.suppression.empty()) return;
    if (top.successors.empty()) return;
    auto& interfaces = base.getIfaceMgr().eigrpInterfaceList;
    for (auto& [id, info] : top.suppression)
    {
        if (auto ifaceIt = interfaces.find(id); ifaceIt != interfaces.end())
            for (auto sum : info.summaries)
                ifaceIt->second.getAggregator().updateSummaryRoute(*sum);
    }
}

void GlobalAggregator::enableAutoSummary(bool enable)
{
    if (base.getAF() != types::AddressFamily::IPv4) return; // Only supported for IPv4

    auto& configs = base.getGlobalConfigMgr();
    bool current = configs.isAutoSummarized();
    if (enable == current) return; // No change

    configs.setAutoSummary(enable);

    // Lock interface for duration
    auto& ifmgr = base.getIfaceMgr();

    if (enable)
    {
        std::set<types::IPPrefix> classfulGroups;

        for (auto& [prefix, top] : base.getTopology().entries())
        {
            if (top.successors.empty())
                continue;
            classfulGroups.emplace(prefix.v4(), prefix.getDefaultMask());
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
} // namespace routing
