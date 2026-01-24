// OspfProcess.cpp

#include "OspfProcess.h"
#include <OspfArea.h>
#include "OspfTopology.h"
#include <Global.h>
#include <VirtualRouter.h>
namespace OSPF
{

OspfProcess::OspfProcess(bool isV3, uint32_t procId, VirtualRouter* vrf)
    : isV3(isV3), routingInstance(vrf), tmgr(vrf->global.timeManager), procId(procId), ifaceMgr(*this)
{
    auto& registry = routingInstance->global.registry;
    auto key = Config::generateOspfKey(routingInstance->instanceId, getProcId(), AddressFamily::NONE, isV3);
    if (isV3 && routingInstance->isDefault)
    {
        v3Configs = registry.create<Config::OspfAddressFamilyV3Registry>(key);
        auto& base = v3Configs.get().template get<Config::OspfAddressFamilyV3::BASE>();
        configs = registry.ensure(base, key);
        auto topo = registry.ensure(configs.get().get<Config::Ospf::BASE>(), key);
        registry.ensure(topo.get().get<Config::OspfTopology::BASE>(), key);
    }
    else
    {
        configs = routingInstance->global.registry.template create<Config::OspfRegistry>(key);
    }
}

Topology* OspfProcess::getTopology(uint8_t tid)
{
    std::shared_lock<std::shared_mutex> lock(topologyMu);
    auto it = topologies.find(tid);
    if (it == topologies.end()) return nullptr;
    return &it->second;
}
    
Topology& OspfProcess::insureTopology(uint8_t tid)
{
    std::unique_lock<std::shared_mutex> lock(topologyMu);
    if (!topologies.contains(tid))
    {
        topologies.try_emplace(tid, *this, tid); //TODO: maybe add pmr
    }
    return topologies.at(tid);
}
}

