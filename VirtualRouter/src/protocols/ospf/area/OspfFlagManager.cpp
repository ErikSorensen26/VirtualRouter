// OspfFlagManager.cpp

#include "OspfFlagManager.h"
#include "OspfArea.h"
#include <OspfInterface.h>

namespace OSPF
{
InterfaceFlagManager::InterfaceFlagManager(OspfInterface& iface)
    : area(iface.getArea()) {}

uint32_t InterfaceFlagManager::getFlags()
{
    return area.getFlags().getFlags() | flags.load(std::memory_order_relaxed);
}
}
