// BgpRegistry.cpp

#include "BgpRegistry.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

namespace Config
{
void BgpNeighborSessionPathAttribute(void* n)
{
    auto& nbr = *static_cast<BGP::Neighbor*>(n);
    nbr.getScheduler().post([&nbr]() {
        nbr.buildAttributeRanges();
    });
}
}
