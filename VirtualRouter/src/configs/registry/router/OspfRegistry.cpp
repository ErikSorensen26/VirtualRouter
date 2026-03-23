// OspfRegistry.cpp

#include "OspfRegistry.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"

namespace config
{
void OspfAreaTypeChange(void* a)
{
    routing::ospf::Area& area = *static_cast<routing::ospf::Area*>(a);
    area.process().getScheduler().post([&area]{
        area.reset();
    });
}

void OspfAreaSycnRanges(void* a)
{
    routing::ospf::Area& area = *static_cast<routing::ospf::Area*>(a);
    area.process().getScheduler().post([&area] {
        area.syncRangeConfig();
    });
}

void OspfSyncNeighbors(void* b)
{
    routing::ospf::OspfProcess& base = *static_cast<routing::ospf::OspfProcess*>(b);
    base.getScheduler().post([&base] {
        base.getIfaceMgr().syncNeighbors();
    });
}

void OspfSyncNetworks(void* b)
{
    routing::ospf::OspfProcess& base = *static_cast<routing::ospf::OspfProcess*>(b);
    base.getScheduler().post([&base] {
        base.getIfaceMgr().refreshInterfaceList();
    });
}

void OspfSyncSummaries(void* b)
{
    routing::ospf::OspfProcess& base = *static_cast<routing::ospf::OspfProcess*>(b);
    base.getScheduler().post([&base] {
        base.syncSummaryConfig();
    });
}
}
