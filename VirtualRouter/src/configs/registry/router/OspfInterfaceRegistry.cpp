// OspfInterfaceRegistry.cpp

#include "OspfInterfaceRegistry.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"

namespace config
{
void OspfInterfaceSyncTimers(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.getProcess().getScheduler().post([&iface] {
        iface.syncTimers();
    });
}

void OspfInterfaceSyncNeighbors(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.getProcess().getScheduler().post([&iface] {
        iface.getNTable().syncUnicast();
    });
}

void OspfInterfaceSyncNetworkType(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.getProcess().getScheduler().post([&iface] {
        iface.syncNetworkType();
    });
}

void OspfInterfaceDemandCircuit(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.getProcess().getScheduler().post([&iface] {
        iface.getArea().runDCIntegrityScan();
        iface.getArea().setFloodReduction(iface);
    });
}

void OspfInterfaceBaseUpdateDigestKey(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.getProcess().getScheduler().post([&iface] {
        iface.syncDigestKey();
    });
}

void OspfInterfaceBasePrefixSuppression(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.getProcess().getScheduler().post([&iface] {
        iface.getArea().getOriginator().updateInterface(iface.id.interfaceId);
    });
}
}
