// OspfInterfaceRegistry.cpp

#include "OspfInterfaceRegistry.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"

namespace config
{
void OspfInterfaceBaseSyncTimers(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterfaceBase*>(ifacePtr);
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

void OspfInterfaceSyncPassive(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncPassive();
}

void OspfGlobalInterfaceBaseUpdateDigestKey(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterfaceBase*>(ifacePtr);
    iface.enqueueSyncDigestKey();
}

void OspfGlobalInterfacePrefixSuppression(void* ifacePtr)
{
    auto& iface = *static_cast<routing::ospf::OspfInterface*>(ifacePtr);
    iface.enqueueSyncPrefixSuppression();
}
}
