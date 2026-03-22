// Interface.h

#ifndef OSPF_INTERFACE_H
#define OSPF_INTERFACE_H

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "ospf/area/FlagManager.h"
#include "ospf/neighbor/NeighborTable.h"
#include "ospf/interface/InterfaceTimers.h"
#include "InterfaceId.hpp"

class Interface;

namespace OSPF
{
class PacketDispatcher;
class OspfProcess;
class Topology;
class Area;
class Neighbor;

class OspfInterface
{
public:
    OspfInterface(OspfProcess& proc, Interface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& configs, const OspfInterfaceId& id);
    ~OspfInterface();

    const OspfInterfaceId id;
    const uint32_t interfaceId;

    OspfProcess& getProcess() { return process; }
    const OspfProcess& getProcess() const { return process; }
    NeighborTable& getNTable() { return ntable; }
    const NeighborTable& getNTable() const { return ntable; }
    InterfaceTimers& getTimers() { return tmgr; }
    PacketDispatcher& getDispatcher() { return *dispatcher; }
    InterfaceFlagManager& getFlags() { return flags; }
    const InterfaceFlagManager& getFlags() const { return flags; }
    InterfaceFlagManager& getLsaFlags() { return lsaFlags; }
    const InterfaceFlagManager& getLsaFlags() const { return lsaFlags; }
    Config::OspfInterfaceRegistry& getConfigs() { return configs.get(); }
    const Config::OspfInterfaceRegistry& getConfigs() const { return configs.get(); }
    Config::OspfInterfaceBaseRegistry& getBaseConfigs() { return baseConfigs.get(); }
    const Config::OspfInterfaceBaseRegistry& getBaseConfigs() const noexcept { return baseConfigs.get(); }

    Area& getArea();
    uint32_t getAreaId() const { return id.area; }
    Interface& getIface() { return iface; }
    const Interface& getIface() const { return iface; }

    const IPPrefix interfaceAddress;

    void election();
    void calculateCost();
    bool setDr(uint32_t dr);
    bool setBdr(uint32_t bdr);
    void syncConfigs();
    void syncTimers();
    void syncNetworkType();
    void syncDigestKey();
    void setPassiveMode(bool passive);

    struct Designation { std::atomic<uint32_t> rid; std::atomic<__uint128_t> ip; };
    Designation dr;
    Designation bdr;

    std::atomic<bool> isDr = false;
    std::atomic<bool> isBdr = false;
    std::atomic<bool> isVirtual = false;

    // Auth
    std::optional<__uint128_t> authKey = std::nullopt;
    std::optional<uint8_t> authKeyId = std::nullopt;

    std::atomic<bool> isMulticast = false;
    std::atomic<bool> opaqueEnabled = true;
    enum class DcDecision { UNDECIDED, ENABLED, DISABLED };

    uint16_t cost;
    std::chrono::seconds helloTime;
    std::chrono::seconds deadTime;
    DcDecision demandCircuit = DcDecision::UNDECIDED;
    bool floodReduction = false;

private:
    PacketDispatcher* dispatcher = nullptr;

    OspfProcess& process;
    Area& area;

    InterfaceFlagManager flags;
    InterfaceFlagManager lsaFlags;
    NeighborTable ntable;
    InterfaceTimers tmgr;
    Interface& iface;

    Config::Reference<Config::OspfInterfaceRegistry> configs;
    Config::Reference<Config::OspfInterfaceBaseRegistry> baseConfigs;
};
}

#endif // OSPF_INTERFACE_H
