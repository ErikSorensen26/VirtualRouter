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

static Neighbor& AddressFamilyInstanceHelper::getNeighbor(uint32_t rid)
{

}
}
