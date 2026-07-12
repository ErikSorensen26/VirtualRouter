// OspfOriginator.cpp

#include <TimeManager.h>
#include <VirtualRouter.h>
#include "IntraOriginator.h"
#include "Area.h"
#include "configs/registry/router/OspfRegistry.h"
#include "interface/Interface.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/neighbor/Neighbor.h"
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"
#include "configs/registry/router/OspfRegistry.h"

namespace routing::ospf
{
IntraOriginator::IntraOriginator(OriginatorContext& ctx)
    : context(ctx)
{}

IntraOriginator::~IntraOriginator()
{}

void IntraOriginator::addRouterLink(LsaBody& router, const OspfInterface& iface, bool refresh, bool attemptNetLsa)
{
    if (iface.getAreaId() != context.area.areaId) return;

    auto& ifaceBaseConfigs = context.getIfaceMgr().getInterfaceBaseConfigs(iface);
    auto& ifaceConfigs = context.getIfaceMgr().getInterfaceConfigs(iface);

    bool prefixSuppression = ifaceBaseConfigs.get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();
    auto ntype = ifaceConfigs.get<config::OspfInterface::NETWORK>().load();

    if (ifaceConfigs.get<config::OspfInterface::PASSIVE>().load() ||
        iface.iface.configs.interfaceType == interface::InterfaceType::LOOPBACK)
    {
        addStubLink(router, iface);
        return;
    }

    const NeighborTable& ntable = context.getIfaceMgr().getNTable(iface);

    if (ntype == config::ospf::NetworkType::POINT_TO_POINT)
    {
        ntable.forEach([this, &iface, &router, prefixSuppression](uint32_t, const Neighbor& nbr) {
            if (nbr.getState() != Neighbor::State::FULL)
                return;

            if (iface.isVirtual)
                addVirtualLink(router, iface, nbr);
            else
            {
                addP2PLink(router, iface, nbr);
                if (!prefixSuppression)
                    addStubLink(router, iface);
            }
        });
        return;
    }

    if (ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT)
    {
        ntable.forEach([this, &iface, &router, prefixSuppression](uint32_t, const Neighbor& nbr) {
            if (nbr.getState() != Neighbor::State::FULL)
                return;

            addP2PLink(router, iface, nbr);
            if (!prefixSuppression)
                addStubLink(router, iface, true);
        });
    }

    if (ntype == config::ospf::NetworkType::BROADCAST ||
        ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        bool isDr = iface.getIsDr();
        bool haveDr = isDr;

        const Neighbor* drNbr = nullptr;
        if (!haveDr)
        {
            drNbr = ntable.lookup(iface.getDrRid());
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
                    addNetworkLsa(iface, refresh);
                    return;
                }
            }
        }
        else if (!prefixSuppression)
        {
            addStubLink(router, iface);
        }

        return;
    }
}
} // namespace routing
