// OspfProcess.cpp

#include "OspfProcess.h"
#include <OspfArea.h>
#include "OspfTopology.h"
#include <Global.h>
#include <VirtualRouter.h>

namespace OSPF
{

OspfProcess::OspfProcess(bool isV3, uint32_t procId, VirtualRouter* vrf)
    : isV3(isV3), routingInstance(vrf), tmgr(vrf->global.timeManager), procId(procId), ifaceMgr(*this),
    cfgs([this, isV3]() -> Config::OspfRegistry& {
        if (isV3)
        {
            auto& bucket = routingInstance->global.registry.bucket<Config::OspfAddressFamilyV3, Config::BASE>();
            v3cfgsHandle = bucket.create();
            v3cfgs = bucket.get(bucket.create());
            return *v3cfgs->get<Config::OspfAddressFamilyV3::BASE>().get().ptr();
        }
        else
        {
            auto& bucket = routingInstance->global.registry.bucket<Config::Ospf, Config::BASE>();
            cfgsHandle = bucket.create();
            return *bucket.get(cfgsHandle);
        }
      }()) {}

OspfProcess::~OspfProcess()
{
    auto& registry = routingInstance->global.registry;
    if (v3cfgs)
    {
        registry.bucket<Config::OspfAddressFamilyV3, Config::BASE>().erase(v3cfgsHandle);
        v3cfgs = nullptr;
    }
    else
    {
        registry.bucket<Config::Ospf, Config::BASE>().erase(cfgsHandle);
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

