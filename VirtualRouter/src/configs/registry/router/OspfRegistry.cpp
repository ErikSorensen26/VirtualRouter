// OspfRegistry.cpp

#include "OspfRegistry.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"

namespace Config
{
void OspfAreaTypeChange(void* a)
{
    OSPF::Area& area = *static_cast<OSPF::Area*>(a);
    area.process().getScheduler().post([&area]{
        area.reset();
    });
}

void OspfAreaSycnRanges(void* a)
{
    OSPF::Area& area = *static_cast<OSPF::Area*>(a);
    area.process().getScheduler().post([&area] {
        area.syncRangeConfig();
    });
}

void OspfSyncNeighbors(void* b)
{
    OSPF::OspfProcess& base = *static_cast<OSPF::OspfProcess*>(b);
    base.getScheduler().post([&base] {
        base.getIfaceMgr().syncNeighbors();
    });
}

void OspfSyncNetworks(void* b)
{
    OSPF::OspfProcess& base = *static_cast<OSPF::OspfProcess*>(b);
    base.getScheduler().post([&base] {
        base.getIfaceMgr().refreshInterfaceList();
    });
}

void OspfSyncSummaries(void* b)
{
    OSPF::OspfProcess& base = *static_cast<OSPF::OspfProcess*>(b);
    base.getScheduler().post([&base] {
        base.syncSummaryConfig();
    });
}
}
