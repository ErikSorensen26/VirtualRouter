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

    const AfiSafi family;

    bool mpNegotiated;

private:
    Config::ReferenceContainer<Config::BgpNeighbor> configs;
};
}

#endif // BGP_NEIGHBOR_AF_H
