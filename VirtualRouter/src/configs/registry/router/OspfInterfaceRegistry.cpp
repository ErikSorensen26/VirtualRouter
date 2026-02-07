// OspfInterfaceRegistry.cpp

#include "OspfInterfaceRegistry.h"
#include <OspfInterface.h>
#include <OspfArea.h>

namespace Config
{
void OspfInterfaceSyncTimers(OSPF::OspfInterface& iface)
{
    iface.syncTimers();
}

void OspfInterfaceSyncNeighbors(OSPF::OspfInterface& iface)
{
    iface.getNTable().syncUnicast();
}

void OspfInterfaceSyncNetworkType(OSPF::OspfInterface& iface)
{
    iface.syncNetworkType();
}

void OspfInterfaceDemandCircuit(OSPF::OspfInterface& iface)
{
    // TODO: add to process queue
    iface.getArea().runDCIntegrityScan();
}

void OspfInterfaceBaseUpdateDigestKey(OSPF::OspfInterface& iface)
{
    iface.syncDigestKey();
}

void OspfInterfaceBasePrefixSuppression(OSPF::OspfInterface& iface)
{
    iface.getArea().getOriginator().updateInterface(iface.id.interfaceId);
}
}
