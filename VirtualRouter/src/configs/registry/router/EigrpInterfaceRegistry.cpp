// EigrpInterfaceRegistry.cpp

#include "EigrpInterfaceRegistry.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/core/Eigrp.h"

namespace Config
{
void EigrpIfacePassive(void* i)
{
    EIGRP::EigrpInterface& iface = *static_cast<EIGRP::EigrpInterface*>(i);
    EIGRP::Eigrp& eigrp = iface.getBase();
    uint32_t key = iface.interfaceKey;
    eigrp.getScheduler().post([&eigrp, key] {
        auto* eigrpIface = eigrp.getIfaceMgr().getInterface(key);
        if (!eigrpIface) return;
        bool passive = eigrpIface->configs->get<Config::EigrpInterface::PASSIVE_INTERFACE>().load();
        eigrpIface->setPassiveMode(passive);
    });
}

void EigrpIfaceShutdown(void* i)
{
    EIGRP::EigrpInterface& iface = *static_cast<EIGRP::EigrpInterface*>(i);
    EIGRP::Eigrp& eigrp = iface.getBase();
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}

void EigrpIfaceSummary(void* i)
{
    EIGRP::EigrpInterface& iface = *static_cast<EIGRP::EigrpInterface*>(i);
    EIGRP::Eigrp& eigrp = iface.getBase();
    uint32_t key = iface.interfaceKey;
    eigrp.getScheduler().post([&eigrp, key] {
        auto* eigrpIface = eigrp.getIfaceMgr().getInterface(key);
        if (!eigrpIface) return;

        std::set<IPPrefix> summaries;
        eigrpIface->configs->get<Config::EigrpInterface::SUMMARY_ADDRESS>().withRead(
            [&](const std::vector<std::tuple<IPAddress, uint8_t>>& v) {
                for (const auto& [addr, len] : v)
                    summaries.emplace(addr, len);
            });
        eigrpIface->getAggregator().installSummaries(summaries);
    });
}
}
