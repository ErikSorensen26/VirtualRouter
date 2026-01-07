// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <OspfTypes.hpp>
#include <OspfInterfaceManager.h>
#include <map>

class VirtualRouter;

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
    OspfProcess(bool v3, uint32_t procId) : isV3(v3), procId(procId), ifaceMgr(*this) {}

    OspfConfigs& getConfigs() { return cfgs; }
    const OspfConfigs& getConfigs() const { return cfgs; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    AddressFamily getAF() const { return af; }
    uint32_t getProcId() const { return procId; }
    uint32_t getRouterId() const { return cfgs.routerId.load(std::memory_order_relaxed); }

    bool calculateRID();

    Topology* getTopology(uint8_t tid);
    Topology& insureTopology(uint8_t tid);

    const bool isV3;

    VirtualRouter* routingInstance = nullptr;

private:
    std::shared_mutex topologyMu;
    std::map<uint8_t, Topology> topologies;

    const uint32_t procId;
    AddressFamily af;
    OspfConfigs cfgs;
    InterfaceManager ifaceMgr;
};
}

#endif // OSPF_H
