// EigrpRegistry.cpp

#include "EigrpRegistry.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"

namespace Config
{
void EigrpSyncNetworks(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}

void EigrpShutdown(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        bool isShutdown = eigrp.getGlobalConfigMgr().getConfigs().get<Config::Eigrp::SHUTDOWN>().load();
        if (isShutdown)
            eigrp.shutdown();
        else
            eigrp.start();
    });
}

void EigrpSyncVariance(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.getTopology().recalculateAll();
    });
}

void EigrpSyncKValues(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.getTopology().recalculateAll();
    });
}

void EigrpSyncNeighbors(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}

void EigrpSyncPassive(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        auto& cfgMgr = eigrp.getGlobalConfigMgr();
        for (auto& [key, iface] : eigrp.getIfaceMgr().eigrpInterfaceList)
            iface.setPassiveMode(cfgMgr.isPassive(key));
    });
}

void EigrpSyncRouterId(void* e)
{
    EIGRP::Eigrp& eigrp = *static_cast<EIGRP::Eigrp*>(e);
    eigrp.getScheduler().post([&eigrp] {
        auto& ridField = eigrp.getGlobalConfigMgr().getConfigs().get<Config::Eigrp::ROUTER_ID>();
        if (ridField.hasValue())
            eigrp.routerID(ridField.load());
        else
            eigrp.clearRouterID();
    });
}
}
