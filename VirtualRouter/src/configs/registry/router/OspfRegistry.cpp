// OspfRegistry.cpp

#include "OspfRegistry.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"

namespace config
{
void OspfAreaTypeChange(void* a)
{
    routing::ospf::Area& area = *static_cast<routing::ospf::Area*>(a);
    area.enqueueReset();
}

void OspfAreaSycnRanges(void* a)
{
    routing::ospf::Area& area = *static_cast<routing::ospf::Area*>(a);
    area.enqueueSyncRanges();
}

void OspfSyncNeighbors(void* b)
{
    routing::ospf::OspfProcess& base = *static_cast<routing::ospf::OspfProcess*>(b);
    base.enqueueSyncNeighbor();
}

void OspfSyncNetworks(void* b)
{
    routing::ospf::OspfProcess& base = *static_cast<routing::ospf::OspfProcess*>(b);
    base.enqueueSyncNetworks();
}

void OspfSyncSummaries(void* b)
{
    routing::ospf::OspfProcess& base = *static_cast<routing::ospf::OspfProcess*>(b);
    base.enqueueSyncSummaries();
}
}
