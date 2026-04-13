// EigrpInterfaceRegistry.cpp

#include "EigrpInterfaceRegistry.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/core/Eigrp.h"
#include "interface/configs/InterfaceType.hpp"

namespace config
{
void EigrpIfacePassive(void* i)
{
    routing::eigrp::EigrpInterface& iface = *static_cast<routing::eigrp::EigrpInterface*>(i);
    routing::eigrp::Eigrp& eigrp = iface.getBase();
    interface::InterfaceKey key = iface.interfaceKey;
    eigrp.getScheduler().post([&eigrp, key] {
        auto* eigrpIface = eigrp.getIfaceMgr().getInterface(key);
        if (!eigrpIface) return;
        bool passive = eigrpIface->configs.reg.get<config::EigrpInterface::PASSIVE_INTERFACE>().load();
        eigrpIface->setPassiveMode(passive);
    });
}

void EigrpIfaceShutdown(void* i)
{
    routing::eigrp::EigrpInterface& iface = *static_cast<routing::eigrp::EigrpInterface*>(i);
    routing::eigrp::Eigrp& eigrp = iface.getBase();
    eigrp.getScheduler().post([&eigrp] {
        eigrp.refreshInterfaceList();
    });
}

void EigrpIfaceSummary(void* i)
{
    routing::eigrp::EigrpInterface& iface = *static_cast<routing::eigrp::EigrpInterface*>(i);
    routing::eigrp::Eigrp& eigrp = iface.getBase();
    interface::InterfaceKey key = iface.interfaceKey;
    eigrp.getScheduler().post([&eigrp, key] {
        auto* eigrpIface = eigrp.getIfaceMgr().getInterface(key);
        if (!eigrpIface) return;

        std::set<types::IPPrefix> summaries;
        eigrpIface->configs.reg.get<config::EigrpInterface::SUMMARY_ADDRESS>().withRead(
            [&](const std::vector<std::tuple<types::IPPrefix, std::optional<std::string>>>& v) {
                for (const auto& [prefix, name] : v)
                    summaries.emplace(prefix);
            });
        eigrpIface->getAggregator().installSummaries(summaries);
    });
}
}
