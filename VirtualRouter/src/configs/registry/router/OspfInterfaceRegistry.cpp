// OspfInterfaceRegistry.cpp

#include "OspfInterfaceRegistry.h"
#include <OspfInterface.h>
#include <OspfArea.h>
#include <OspfProcess.h>

namespace Config
{
void OspfInterfaceSyncTimers(OSPF::OspfInterface& iface)
{
    iface.getProcess().getScheduler().post([&iface] {
        iface.syncTimers();
    });
}

void OspfInterfaceSyncNeighbors(OSPF::OspfInterface& iface)
{
    iface.getProcess().getScheduler().post([&iface] {
        iface.getNTable().syncUnicast();
    });
}

void OspfInterfaceSyncNetworkType(OSPF::OspfInterface& iface)
{
    iface.getProcess().getScheduler().post([&iface] {
        iface.syncNetworkType();
    });
}

void OspfInterfaceDemandCircuit(OSPF::OspfInterface& iface)
{
    iface.getProcess().getScheduler().post([&iface] {
        iface.getArea().runDCIntegrityScan();
    });
}

void OspfInterfaceBaseUpdateDigestKey(OSPF::OspfInterface& iface)
{
    iface.getProcess().getScheduler().post([&iface] {
        iface.syncDigestKey();
    });
}

void OspfInterfaceBasePrefixSuppression(OSPF::OspfInterface& iface)
{
    iface.getProcess().getScheduler().post([&iface] {
        iface.getArea().getOriginator().updateInterface(iface.id.interfaceId);
    });
}
}
