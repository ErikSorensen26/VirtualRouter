// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <Registry.hpp>
#include <OspfTypes.hpp>
#include <OspfInterfaceManager.h>
#include <OspfRoutingTable.h>
#include <RegistryTypes.hpp>
#include <variant>
#include <map>

class VirtualRouter;
class TimeManager;

namespace OSPF 
{
class Topology;
class OspfProcess;
class OspfInterface;
class OspfArea;

struct OspfV3Instance
{
    OspfProcess* ipv4 = nullptr;
    OspfProcess* ipv6 = nullptr;
};

struct OspfInterfaceInstance
{
    OspfInterface* IPv4; ///< Pointer to the IPv4 EIGRP interface.
    OspfInterface* IPv6; ///< Pointer to the IPv6 EIGRP interface.
};

class OspfProcess
{
public:
    friend class Topology;

    using V3AfConfigs = Config::Reference<Config::OspfAddressFamilyV3Registry>;
    using V2AfConfigs = Config::Reference<Config::OspfAddressFamilyV2Registry>;

    OspfProcess(bool isV3, uint32_t procId, AddressFamily af, VirtualRouter* vrf);

    AddressFamily getAF() { return af; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    const InterfaceManager& getIfaceMgr() const noexcept { return ifaceMgr; }
    Config::OspfRegistry& getConfigs() { return configs.get(); }
    const Config::OspfRegistry& getConfigs() const noexcept { return configs.get(); }

    uint32_t getProcId() const { return procId; }
    uint32_t getRouterId() const
    {
        const auto& id = configs.get().get<Config::Ospf::ROUTER_ID>();
        if (id.hasValue()) return id.load();
        return rid.load(std::memory_order_relaxed);
    }

    bool calculateRID();

    Topology* getTopology(uint8_t tid);
    Topology& insureTopology(uint8_t tid);

    const bool isV3;

    VirtualRouter* routingInstance = nullptr;

    TimeManager& tmgr;

    std::shared_mutex topologyMu;
    std::map<uint8_t, Topology> topologies;

private:
    friend class Topology;

    std::atomic<uint32_t> rid;

    const uint32_t procId;
    const AddressFamily af;
    InterfaceManager ifaceMgr;

    std::variant<std::monostate, V3AfConfigs, V2AfConfigs> afConfigs;
    Config::Reference<Config::OspfRegistry> configs;
};
}

#endif // OSPF_H
