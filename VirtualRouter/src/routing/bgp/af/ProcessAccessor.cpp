// ProcessAccessor.cpp

#include "ProcessAccessor.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
core::VirtualRouter& ProcessAccessor::getRoutingInstance(BgpProcess& proc)
{
    return *proc.routingInstance;
}

NeighborTable& ProcessAccessor::getNtable(BgpProcess& proc)
{
    return proc.getNtable();
}

uint32_t ProcessAccessor::getAsNum(BgpProcess& proc)
{
    return proc.asNumber;
}

uint32_t ProcessAccessor::getRid(BgpProcess& proc)
{
    return proc.getRouterId();
}

config::BgpRegistry& ProcessAccessor::getConfigs(BgpProcess& proc)
{
    return proc.getConfigs();
}

AttributeManager& ProcessAccessor::getAttrMgr(BgpProcess& proc)
{
    return proc.getAttrMgr();
}

core::ProcessQueueRef ProcessAccessor::getScheduler(BgpProcess& proc)
{
    return proc.getScheduler();
}

void ProcessAccessor::emplaceAfBase(config::ReferenceContainer<config::BgpAfBaseRegistry CONFIG_INDEX_PARAM>& base, BgpProcess& proc)
{
    proc.routingInstance->getRegistry().emplace(base);
}
} // namespace routing
