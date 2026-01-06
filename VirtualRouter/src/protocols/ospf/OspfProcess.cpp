// OspfProcess.cpp

#include "OspfProcess.h"
#include <OspfArea.h>
#include "OspfTopology.h"

namespace OSPF
{
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

