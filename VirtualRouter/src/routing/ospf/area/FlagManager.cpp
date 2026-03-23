// OspfFlagManager.cpp

#include "FlagManager.h"
#include "Area.h"
#include "ospf/interface/OspfInterface.h"

namespace routing::ospf
{
InterfaceFlagManager::InterfaceFlagManager(OspfInterface& iface)
    : area(iface.getArea()) {}

uint32_t InterfaceFlagManager::getFlags()
{
    return area.getFlags().getFlags() | flags.load(std::memory_order_relaxed);
}
} // namespace routing
