// OspfRegistry.cpp

#include "OspfRegistry.h"
#include "ospf/OspfProcess.h"
#include "ospf/area/Area.h"

namespace Config
{
void OspfAreaTypeChange(OSPF::Area& area)
{
    area.process().getScheduler().post([&area]{
        area.reset();
    });
}

void OspfAreaSycnRanges(OSPF::Area& area)
{
    area.process().getScheduler().post([&area] {
        area.syncRangeConfig();
    });
}

void OspfSyncNeighbors(OSPF::OspfProcess& base)
{
    base.getScheduler().post([&base] {
        base.getIfaceMgr().syncNeighbors();
    });
}

void OspfSyncNetworks(OSPF::OspfProcess& base)
{
    base.getScheduler().post([&base] {
        base.getIfaceMgr().refreshInterfaceList();
    });
}

void OspfSyncSummaries(OSPF::OspfProcess& base)
{
    base.getScheduler().post([&base] {
        base.syncSummaryConfig();
    });
}
}
