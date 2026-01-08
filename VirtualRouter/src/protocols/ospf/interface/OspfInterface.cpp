// OspfInterface.cpp

#include "OspfInterface.h"
#include <OspfProcess.h>
#include <Interface.h>
#include <OspfTopology.h>

#include <v2PacketDispatcher.h>
#include <v3PacketDispatcher.h>

auto getIfaceAddr(Interface& iface, AddressFamily af) -> IPPrefix
{
    return af == AddressFamily::IPv4
        ? iface.configs.ipv4.getAddressMask()
        : iface.configs.ipv6.getLocalPrefix();
}

namespace OSPF
{
OspfInterface::OspfInterface(OspfProcess& proc, Interface& iface, OspfInterfaceId& id)
    : process(proc),
      topology(&proc.insureTopology(iface.configs.tid.load(std::memory_order_relaxed))),
      id(id),
      interfaceAddress(getIfaceAddr(iface, process.getAF())),
      tmgr(proc.tmgr, *this),
      iface(iface)
{
    dispatcher = proc.isV3
        ? new PacketDispatcherV3(*this)
        : new PacketDispatcherV2(*this);
}

OspfInterface::~OspfInterface()
{
    delete dispatcher;
}

OspfArea& OspfInterface::getArea()
{
    return topology.load(std::memory_order_relaxed)->insureArea(getAreaId());
}
}
