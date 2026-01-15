// OspfInterface.h

#ifndef OSPF_INTERFACE_H
#define OSPF_INTERFACE_H

#include <OspfTypes.hpp>
#include <OspfNeighborTable.h>
#include <PacketDispatcher.h>
#include "OspfInterfaceTimers.h"
#include "OspfInterfaceId.hpp"

class Interface;

namespace OSPF
{
class OspfProcess;
class Topology;
class OspfArea;
class OspfInterface
{
public:
    OspfInterface(OspfProcess& proc, Interface& iface, OspfInterfaceId& areaId);
    ~OspfInterface();

    OspfProcess& process;
    std::atomic<Topology*> topology;
    const OspfInterfaceId id;

    InterfaceConfigs* configs = nullptr;

    NeighborTable& getNTable() { return ntable; }
    const NeighborTable& getNTable() const { return ntable; }
    InterfaceTimers& getTimers() { return tmgr; }
    PacketDispatcher& getDispatcher() { return *dispatcher; }

    OspfArea& getArea();
    uint32_t getAreaId() const { return id.area; }
    Interface& getIface() { return iface; }
    const Interface& getIface() const { return iface; }

    const IPPrefix interfaceAddress;

    void election();

    struct Designation { std::atomic<uint32_t> rid; std::atomic<__uint128_t> ip; };
    Designation dr;
    Designation bdr;

    std::atomic<bool> isDr = false;
    std::atomic<bool> isBdr = false;
    std::atomic<bool> isVirtual = false;

private:
    PacketDispatcher* dispatcher = nullptr;

    NeighborTable ntable;
    InterfaceTimers tmgr;
    Interface& iface;
};
}

#endif // OSPF_INTERFACE_H
