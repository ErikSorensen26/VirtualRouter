// OspfProcess.cpp

#include <Registry.hpp>
#include "OspfProcess.h"
#include <OspfArea.h>
#include "OspfTopology.h"
#include <Global.h>
#include <VirtualRouter.h>

namespace OSPF
{

OspfProcess::OspfProcess(bool isV3, uint32_t procId, AddressFamily af, VirtualRouter* vrf)
    : isV3(isV3), routingInstance(vrf), tmgr(vrf->global.timeManager), procId(procId), af(af), ifaceMgr(*this),
    configs([this, isV3]() {
        auto& registry = routingInstance->global.registry;
        auto key = Config::generateOspfKey(routingInstance->instanceId, getProcId(), getAF(), isV3);
        if (isV3)
        {
            if (routingInstance->isDefault)
            {
                // TODO: add address family v3 configs from elsewhere
                auto& afCfgs = std::get<V3AfConfigs>(afConfigs);
                auto base = registry.ensure(afCfgs->get<Config::OspfAddressFamilyV3::BASE>(), key);
                auto topo = registry.ensure(base->get<Config::Ospf::BASE>(), key);
                registry.ensure(topo->get<Config::OspfTopology::BASE>(), key);
                return base;
            }
            // OSPFv3 VRF mode does not support address families
            auto base =  registry.create<Config::OspfRegistry>(key);
            auto topo = registry.ensure(base->get<Config::Ospf::BASE>(), key);
            registry.ensure(topo->get<Config::OspfTopology::BASE>(), key);
            return base;
        }
        else
        {
            // OSPFv2 AddressFamily
            afConfigs.emplace<V2AfConfigs>(registry.create<Config::OspfAddressFamilyV2Registry>(key));
            auto& afCfgs = std::get<V2AfConfigs>(afConfigs);
            auto& v2Base = afCfgs->get<Config::OspfAddressFamilyV2::BASE>();
            auto base = registry.ensure(v2Base, key);
            auto topo = registry.ensure(base->get<Config::Ospf::BASE>(), key);
            registry.ensure(topo->get<Config::OspfTopology::BASE>(), key);
            return base;
        }
    }())
{}

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

