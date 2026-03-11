// BgpRegistry.cpp

#include "BgpRegistry.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

namespace Config
{
void BgpNeighborSessionShutdown(void* n)
{
    auto& nbr = *static_cast<BGP::Neighbor*>(n);
    nbr.getScheduler().post([&nbr]() {
        if (nbr.getConfigs().get<Config::BgpNeighborSession::SHUTDOWN>().load())
            nbr.getProcess().shutdownNeighbor(nbr);
        else
            nbr.getProcess().unshutdownNeighbor(nbr);
    });
}

void BgpNeighborSessionPathAttribute(void* n)
{
    auto& nbr = *static_cast<BGP::Neighbor*>(n);
    nbr.getScheduler().post([&nbr]() {
        nbr.buildAttributeRanges();
    });
}
}
