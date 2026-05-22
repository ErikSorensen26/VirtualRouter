// BgpRegistry.cpp

#include "BgpRegistry.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/BgpProcess.h"

namespace config
{
void BgpNeighborDefaultOriginate(void* n)
{
    auto& nbr = *static_cast<routing::bgp::NeighborAf*>(n);
    if (!nbr.globalNbr().session || !nbr.globalNbr().session->established())
        return;
    nbr.globalNbr().getScheduler().post([&nbr]() {
        std::visit([&nbr](auto& af){
            if (nbr.getConfigs().get<config::BgpAfBase::DEFAULT_ORIGINATE>().load())
                af.sendDefaultOriginate(*nbr.globalNbr().session);
            else
                af.withdrawDefaultOriginate(*nbr.globalNbr().session);
        }, nbr.getAddressFamily());
    });
}

void BgpNeighborSessionShutdown(void* n)
{
    auto& nbr = *static_cast<routing::bgp::Neighbor*>(n);
    nbr.getScheduler().post([&nbr]() {
        if (nbr.getConfigs().get<config::BgpNeighborSession::SHUTDOWN>().load())
            nbr.getProcess().shutdownNeighbor(nbr);
        else
            nbr.getProcess().unshutdownNeighbor(nbr);
    });
}

void BgpNeighborSessionPathAttribute(void* n, const std::tuple<bool, uint8_t, uint8_t>& t)
{
    auto& nbr = *static_cast<routing::bgp::Neighbor*>(n);
    nbr.getScheduler().post([&nbr]() {
        nbr.buildAttributeRanges();
    });
}
}
