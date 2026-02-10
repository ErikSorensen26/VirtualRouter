// OspfRegistry.cpp

#include "OspfRegistry.h"
#include <OspfProcess.h>
#include <OspfArea.h>

namespace Config
{
void OspfAreaTypeChange(OSPF::OspfArea& area)
{
    area.process().getScheduler().post([&area]{
        area.reset();
    });
}

void OspfAreaSycnRanges(OSPF::OspfArea& area)
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
