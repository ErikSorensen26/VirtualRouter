// Neighbor.cpp

#include <VirtualRouter.h>

#include "configs/Registry.hpp"
#include "Neighbor.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Neighbor::Neighbor(const IPAddress& ipAddress, BgpProcess& proc)
    : neighborAddress(ipAddress),
      process(proc),
      configs([&proc, &ipAddress]() {
          auto& procConfigs = proc.getConfigs();
          auto& neighborConfigs = procConfigs.get<Config::Bgp::NEIGHBOR>();
          return proc.routingInstance->getRegistry().emplaceBack(neighborConfigs, ipAddress, readU128(ipAddress.raw));
      }())
{
}

Neighbor::~Neighbor()
{
    process.getConfigs().get<Config::Bgp::NEIGHBOR>().erase(configs.getKey());
}
}
