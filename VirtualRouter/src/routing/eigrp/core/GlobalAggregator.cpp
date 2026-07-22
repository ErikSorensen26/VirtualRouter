// GlobalAggregator.cpp

#include "GlobalAggregator.h"
#include "InterfaceManager.h"
#include "Eigrp.h"
#include "Topology.h"
#include "eigrp/interface/RouteAggregator.h"
#include "eigrp/interface/EigrpInterface.h"

namespace routing::eigrp
{
GlobalAggregator::GlobalAggregator(Eigrp& process) : process(process) {}

void GlobalAggregator::addSummary(TopologyEntry& top)
{
    for (auto& [id, iface] : process.getIfaceMgr().eigrpInterfaceList)
    {
        if (auto* sum = iface.aggregator.isSummarized(top.prefix); sum)
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
    auto& interfaces = process.getIfaceMgr().eigrpInterfaceList;
    for (auto& [id, info] : top.suppression)
    {
        if (auto ifaceIt = interfaces.find(id); ifaceIt != interfaces.end())
            for (auto sum : info.summaries)
                ifaceIt->second.aggregator.updateSummaryRoute(*sum);
    }
}

void GlobalAggregator::enableAutoSummary(bool enable)
{
    if (process.addressFamily != types::AddressFamily::IPv4) return; // Only supported for IPv4

    bool current = process.configs.get<config::Eigrp::AUTO_SUMMARIZATION>().load();
    if (enable == current) return; // No change

    process.configs.get<config::Eigrp::AUTO_SUMMARIZATION>().set(enable);

    // Lock interface for duration
    auto& ifmgr = process.getIfaceMgr();

    if (enable)
    {
        std::set<types::IPPrefix> classfulGroups;

        for (auto& [prefix, top] : process.getTopology().entries())
        {
            if (top.successors.empty())
                continue;
            classfulGroups.emplace(prefix.v4(), prefix.getDefaultMask());
        }

        for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
        {
            iface.aggregator.installSummaries(classfulGroups, true);
        }
    }
    else
    {
        for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
        {
            iface.aggregator.clearAutoSummaries();
        }
    }
}

void GlobalAggregator::recomputeAutoSummaries()
{
    auto& ifmgr = process.getIfaceMgr();
    for (auto& [_, iface] : ifmgr.eigrpInterfaceList)
    {
        iface.aggregator.updateAllSummaryRoutes(true);
    }
}
} // namespace routing
