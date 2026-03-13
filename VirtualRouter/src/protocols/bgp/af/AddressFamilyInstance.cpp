// AddressFamilyInstance.cpp

#include "AddressFamilyInstance.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
VirtualRouter& AddressFamilyInstanceHelper::getRoutingInstance(BgpProcess& proc)
{
    return *proc.routingInstance;
}

NeighborTable& AddressFamilyInstanceHelper::getNtable(BgpProcess& proc)
{
    return proc.getNtable();
}

uint32_t AddressFamilyInstanceHelper::getAsNum(BgpProcess& proc)
{
    return proc.asNumber;
}

uint32_t AddressFamilyInstanceHelper::getRid(BgpProcess& proc)
{
    return proc.getRouterId();
}

Config::BgpRegistry& AddressFamilyInstanceHelper::getConfigs(BgpProcess& proc)
{
    return proc.getConfigs();
}

AttributeManager& AddressFamilyInstanceHelper::getAttrMgr(BgpProcess& proc)
{
    return proc.getAttrMgr();
}

ProcessQueueRef AddressFamilyInstanceHelper::getScheduler(BgpProcess& proc)
{
    return proc.getScheduler();
}

void AddressFamilyInstanceHelper::emplaceAfBase(Config::ReferenceContainer<Config::BgpAfBaseRegistry CONFIG_INDEX_PARAM>& base, BgpProcess& proc)
{
    proc.routingInstance->getRegistry().emplace(base);
}
}
