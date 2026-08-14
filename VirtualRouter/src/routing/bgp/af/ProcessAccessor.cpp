// ProcessAccessor.cpp

#include "ProcessAccessor.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
core::VirtualRouter& ProcessAccessor::getRoutingInstance(BgpProcess& proc)
{
    return proc.routingInstance;
}

NeighborTable& ProcessAccessor::getNtable(BgpProcess& proc)
{
    return proc.ntable;
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
    return proc.configs;
}

AttributeManager& ProcessAccessor::getAttrMgr(BgpProcess& proc)
{
    return proc.attrMgr;
}

core::ProcessQueue& ProcessAccessor::getScheduler(BgpProcess& proc)
{
    return proc.scheduler;
}
} // namespace routing
