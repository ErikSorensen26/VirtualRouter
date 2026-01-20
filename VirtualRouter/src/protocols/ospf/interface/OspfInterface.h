// OspfInterface.h

#ifndef OSPF_INTERFACE_H
#define OSPF_INTERFACE_H

#include <OspfTypes.hpp>
#include <OspfNeighborTable.h>
#include <PacketDispatcher.h>
#include <OspfFlagManager.h>
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
    OspfInterface(OspfProcess& proc, Interface& iface, InterfaceConfigs& configs, OspfInterfaceId& id);
    ~OspfInterface();
    OspfProcess& process;
    std::atomic<Topology*> topology;
    std::atomic<OspfArea*> area;
    const OspfInterfaceId id;

    NeighborTable& getNTable() { return ntable; }
    const NeighborTable& getNTable() const { return ntable; }
    InterfaceTimers& getTimers() { return tmgr; }
    PacketDispatcher& getDispatcher() { return *dispatcher; }
    const InterfaceFlagManager& getFlags() const { return flags; }
    InterfaceFlagManager& getFlags() { return flags; }
    const InterfaceFlagManager& getLsaFlags() const { return lsaFlags; }
    InterfaceFlagManager& getLsaFlags() { return lsaFlags; }

    OspfArea& getArea();
    uint32_t getAreaId() const { return id.area; }
    Interface& getIface() { return iface; }
    const Interface& getIface() const { return iface; }

    const IPPrefix interfaceAddress;

    void election();
    bool setDr(uint32_t dr);
    bool setBdr(uint32_t bdr);
    void syncConfigs();
    void setPassiveMode(bool passive);

    struct Designation { std::atomic<uint32_t> rid; std::atomic<__uint128_t> ip; };
    Designation dr;
    Designation bdr;

    std::atomic<bool> isDr = false;
    std::atomic<bool> isBdr = false;
    std::atomic<bool> isVirtual = false;

    std::atomic<bool> isMulticast = false;
    std::atomic<bool> opaqueEnabled = true;
    enum class DcDecision { UNDECIDED, ENABLED, DISABLED };

    std::atomic<DcDecision> demandCircuit = DcDecision::UNDECIDED;
    std::atomic<bool> floodReduction = false;

    InterfaceConfigs& configs;
private:
    PacketDispatcher* dispatcher = nullptr;

    InterfaceFlagManager flags;
    InterfaceFlagManager lsaFlags;
    NeighborTable ntable;
    InterfaceTimers tmgr;
    Interface& iface;
};
}

#endif // OSPF_INTERFACE_H
