// OspfOriginator.cpp

#include "OspfOriginator.h" #include <OspfArea.h>
#include <OspfTopology.h>
#include <OspfProcess.h>
#include <OspfInterface.h>
#include <OspfNeighbor.h>

#include <Interface.h>
#include <InterfaceType.hpp>

namespace OSPF
{
void OspfOriginator::addRouterLink(LsaBody& router, const OspfInterface& iface, bool attemptNetLsa)
{
    if (iface.getAreaId() != area.areaId) return;

    auto ntype = iface.configs->networkType.load(std::memory_order_relaxed);
    const auto& ntable = iface.getNTable();

    if (iface.configs->isPassive.load(std::memory_order_relaxed) ||
        iface.getIface().configs.interfaceType == InterfaceType::LOOPBACK)
    {
        addStubLink(router, iface);
        return;
    }

    if (ntype == InterfaceConfigs::NetworkType::POINT_TO_POINT)
    {
        bool isVirtual = iface.isVirtual.load(std::memory_order_relaxed);
        std::shared_lock<std::shared_mutex> nlock(ntable.mu);
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() != Neighbor::State::FULL)
                continue;

            if (isVirtual)
                addVirtualLink(router, iface, nbr);
            else
                addP2PLink(router, iface, nbr);
        }
        return;
    }

    if (ntype == InterfaceConfigs::NetworkType::BROADCAST ||
        ntype == InterfaceConfigs::NetworkType::NON_BROADCAST)
    {
        bool isDr = iface.isDr.load(std::memory_order_relaxed);
        bool haveDr = isDr;

        const Neighbor* drNbr = nullptr;
        if (!haveDr)
        {
            drNbr = ntable.lookup(iface.dr.rid.load(std::memory_order_relaxed));
            if (drNbr && drNbr->getState() == Neighbor::State::FULL)
                haveDr = true;
        }

        // ALL routers advertise a transit link if the network is operational
        if (haveDr)
        {
            addTransitLink(router, iface, drNbr);
            if (isDr)
            {
                if (attemptNetLsa)
                {
                    addNetworkLsa(iface);
                    return;
                }
            }
        }

        return;
    }
}
}
