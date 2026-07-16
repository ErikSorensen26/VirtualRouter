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

core::ProcessQueue& ProcessAccessor::getScheduler(BgpProcess& proc)
{
    return proc.getScheduler();
}

core::ProcessQueue& ProcessAccessor::getSchedulerQueue(BgpProcess& proc)
{
    return proc.getScheduler();
}
} // namespace routing
