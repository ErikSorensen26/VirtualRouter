// NeighborAf.cpp

#include "NeighborAf.h"
#include "Neighbor.h"

#include "bgp/BgpProcess.h"

namespace BGP
{
NeighborAf::NeighborAf(const AfiSafi& fam, Neighbor& p)
    : family(fam),
      mpNegotiated(false),
      parent(p),
      configs([&p, &fam]() {
          auto& neighborConfigs = p.configs->get<Config::BgpNeighborSession::AF_NEIGHBOR>();
          auto key = Config::generateBgpNeighborKey(p.configs.getKey(), fam.afi, fam.safi);
          uint32_t id = fam.afi | uint32_t(fam.afi) << 16;
          return p.getProcess().routingInstance->getRegistry().emplaceBack(neighborConfigs, id, key);
      }())
{
    (void)parent;
}

NeighborAf::~NeighborAf()
{
    parent.getConfigs().get<Config::BgpNeighborSession::AF_NEIGHBOR>().erase(
        family.afi | uint32_t(family.afi << 16));
}
}
