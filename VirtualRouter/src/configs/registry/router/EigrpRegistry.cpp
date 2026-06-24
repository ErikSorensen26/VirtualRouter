// EigrpRegistry.cpp

#include "EigrpRegistry.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"

namespace config
{
void EigrpSyncNetworks(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}

void EigrpShutdown(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        bool isShutdown = eigrp.getGlobalConfigMgr().getConfigs().get<config::Eigrp::SHUTDOWN>().load();
        if (isShutdown)
            eigrp.shutdownInternal();
        else
            eigrp.start();
    });
}

void EigrpSyncVariance(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.getTopology().recalculateAll();
    });
}

void EigrpSyncKValues(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.getTopology().recalculateAll();
    });
}

void EigrpSyncNeighbors(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}

void EigrpSyncPassive(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        auto& cfgMgr = eigrp.getGlobalConfigMgr();
        for (auto& [key, iface] : eigrp.getIfaceMgr().eigrpInterfaceList)
            iface.setPassiveMode(cfgMgr.isPassive(key));
    });
}

void EigrpSyncRouterId(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        auto ridField = eigrp.getGlobalConfigMgr().getConfigs().get<config::Eigrp::ROUTER_ID>();
        if (ridField.hasValue())
            eigrp.routerID(ridField.load());
        else
            eigrp.clearRouterID();
    });
}

void EigrpSyncAfInterface(void* e)
{
    routing::eigrp::Eigrp& eigrp = *static_cast<routing::eigrp::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}
}
