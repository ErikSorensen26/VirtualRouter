// NeighborAf.h

#ifndef BGP_NEIGHBOR_AF_H
#define BGP_NEIGHBOR_AF_H

#include "bgp/BgpTypes.hpp"
#include "configs/registry/router/BgpRegistry.h"

namespace BGP
{

class Neighbor;
class BgpProcess;

class NeighborAf
{
public:
    NeighborAf(const AfiSafi& family, Neighbor& parent);
    ~NeighborAf();

    const AfiSafi family;

    Config::BgpNeighborRegistry& getConfigs() { return configs.get(); }
    const Config::BgpNeighborRegistry& getConfigs() const { return configs.get(); }
    Neighbor& globalNbr() { return parent; }
    const Neighbor& globalNbr() const { return parent; }

    bool mpNegotiated;

private:
    Neighbor& parent;
    Config::Reference<Config::BgpNeighborRegistry> configs;
};
}

#endif // BGP_NEIGHBOR_AF_H
