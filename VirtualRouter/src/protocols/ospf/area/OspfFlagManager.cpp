// OspfFlagManager.cpp

#include "OspfFlagManager.h"
#include "OspfArea.h"
#include <OspfInterface.h>

namespace OSPF
{
InterfaceFlagManager::InterfaceFlagManager(OspfInterface& iface)
    : area(iface.area) {}

uint32_t InterfaceFlagManager::getFlags()
{
    return area.load(std::memory_order_relaxed)->getFlags().getFlags() | flags.load(std::memory_order_relaxed);
}
}
