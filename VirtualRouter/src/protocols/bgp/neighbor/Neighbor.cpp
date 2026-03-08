// Neighbor.cpp

#include <VirtualRouter.h>

#include "configs/Registry.hpp"
#include "Neighbor.h"
#include "NeighborAf.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Neighbor::Neighbor(const IPAddress& ipAddress, BgpProcess& proc)
    : neighborAddress(ipAddress),
      process(proc),
      configs([&proc, &ipAddress]() {
          auto& procConfigs = proc.getConfigs();
          auto& neighborConfigs = procConfigs.get<Config::Bgp::NEIGHBOR>();
          auto key = Config::generateBgpSessionKey(proc.routingInstance->getInstanceId(), ipAddress);
          return proc.routingInstance->getRegistry().emplaceBack(neighborConfigs, ipAddress, key);
      }())
{
    // Mask base configs
    proc.routingInstance->getRegistry().ensure(
        configs->get<Config::BgpNeighborSession::BGP_BASE>(),
        proc.getConfigs().get<Config::Bgp::BGP_BASE>().local(),
        configs.getKey()
    );
}

Neighbor::~Neighbor()
{
    process.getConfigs().get<Config::Bgp::NEIGHBOR>().erase(neighborAddress);
}

void Neighbor::addAfNeighbor(AfiSafi& afi)
{
    afNeighbors.emplace(afi, afi, *this);
}

void Neighbor::delAfNeighbor(AfiSafi& afi)
{
    afNeighbors.erase(afi);
}

NeighborAf& Neighbor::getAfNeighbor(AfiSafi& afi)
{
    auto it = afNeighbors.find(afi);
    assert(it != afNeighbors.end());
    return  it->second;
}

const NeighborAf& Neighbor::getAfNeighbor(AfiSafi& afi) const
{
    auto it = afNeighbors.find(afi);
    assert(it != afNeighbors.end());
    return  it->second;
}
}
