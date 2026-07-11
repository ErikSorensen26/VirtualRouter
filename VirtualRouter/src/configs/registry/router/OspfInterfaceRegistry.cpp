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
    iface.enqueueSyncTimers();
}

void OspfInterfaceSyncNeighbors(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncUnicastNeighbors();
}

void OspfInterfaceSyncNetworkType(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncNetworkType();
}

void OspfInterfaceDemandCircuit(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncDemandCircuit();
}

void OspfInterfaceBaseUpdateDigestKey(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncDigestKey(); 
}

void OspfInterfaceBasePrefixSuppression(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncPrefixSuppression();
}
}
