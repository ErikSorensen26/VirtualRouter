// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <OspfTypes.hpp>
#include <OspfInterfaceManager.h>
#include <OspfRoutingTable.h>
#include <OspfRegistry.hpp>
#include <RegistryTypes.hpp>
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

    OspfProcess(bool isV3, uint32_t procId, VirtualRouter* vrf);
    ~OspfProcess();

    Config::OspfAddressFamilyV3Registry* getV3Configs() { return v3cfgs; }
    Config::OspfRegistry& configs() { return cfgs; }
    const Config::OspfRegistry& configs() const { return cfgs; }

    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    const InterfaceManager& getIfaceMgr() const noexcept { return ifaceMgr; }
    uint32_t getProcId() const { return procId; }
    uint32_t getRouterId() const {
        auto& id = cfgs.template get<Config::Ospf::ROUTER_ID>();
        if (id.hasValue()) return id.load();
        return rid.load(std::memory_order_relaxed);
    }


    bool calculateRID();

    Topology* getTopology(uint8_t tid);
    Topology& insureTopology(uint8_t tid);

    const bool isV3;

    VirtualRouter* routingInstance = nullptr;

    TimeManager& tmgr;

private:
    std::shared_mutex topologyMu;
    std::map<uint8_t, Topology> topologies;
    std::atomic<uint32_t> rid;

    const uint32_t procId;
    InterfaceManager ifaceMgr;

    Config::Bucket<Config::OspfRegistry>::Handle cfgsHandle;
    Config::OspfRegistry& cfgs;

    Config::Bucket<Config::OspfAddressFamilyV3Registry>::Handle v3cfgsHandle;
    Config::OspfAddressFamilyV3Registry* v3cfgs{nullptr};
};
}

#endif // OSPF_H
