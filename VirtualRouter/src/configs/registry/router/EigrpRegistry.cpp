// EigrpRegistry.cpp

#include "EigrpRegistry.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"

namespace config
{
void EigrpSyncNetworks(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueRefreshInterfaceList();
}

void EigrpShutdown(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueShutdown();
}

void EigrpSyncVariance(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueSyncTopology();
}

void EigrpSyncKValues(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueSyncTopology();
}

void EigrpSyncNeighbors(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueRefreshInterfaceList();
}

void EigrpSyncPassive(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueSyncPassive();
}

void EigrpSyncRouterId(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueSyncRouterId();
}

void EigrpSyncAfInterface(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.enqueueRefreshInterfaceList();
}
}
