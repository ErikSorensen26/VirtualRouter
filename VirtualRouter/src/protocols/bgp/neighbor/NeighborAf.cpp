// NeighborAf.cpp

#include "NeighborAf.h"
#include "Neighbor.h"

namespace BGP
{
NeighborAf::NeighborAf(const AfiSafi& fam, Neighbor& parent)
    : family(fam),
      mpNegotiated(false)
{
    (void)parent;
}
}
