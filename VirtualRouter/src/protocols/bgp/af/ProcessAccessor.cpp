// ProcessAccessor.cpp

#include "ProcessAccessor.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
VirtualRouter& ProcessAccessor::getRoutingInstance(BgpProcess& proc)
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

Config::BgpRegistry& ProcessAccessor::getConfigs(BgpProcess& proc)
{
    return proc.getConfigs();
}

AttributeManager& ProcessAccessor::getAttrMgr(BgpProcess& proc)
{
    return proc.getAttrMgr();
}

ProcessQueueRef ProcessAccessor::getScheduler(BgpProcess& proc)
{
    return proc.getScheduler();
}

void ProcessAccessor::emplaceAfBase(Config::ReferenceContainer<Config::BgpAfBaseRegistry CONFIG_INDEX_PARAM>& base, BgpProcess& proc)
{
    proc.routingInstance->getRegistry().emplace(base);
}
}
