// NeighborAf.h

#ifndef BGP_NEIGHBOR_AF_H
#define BGP_NEIGHBOR_AF_H

#include "bgp/BgpTypes.hpp"
#include "NeighborAfConfigs.hpp"

namespace BGP
{

class Neighbor;
class BgpProcess;
class PeerGroup;
class PeerPolicyTemplate;

class NeighborAf
{
public:
    NeighborAf(const AfiSafi& family, Neighbor& parent);
    ~NeighborAf();

    const AfiSafi family;

    NeighborAfConfigs& getConfigs() { return configs; }
    const NeighborAfConfigs& getConfigs() const { return configs; }
    Neighbor& globalNbr() { return parent; }
    const Neighbor& globalNbr() const { return parent; }

    bool mpNegotiated;

private:
    Neighbor& parent;
    NeighborAfConfigs configs;
};
}

#endif // BGP_NEIGHBOR_AF_H
