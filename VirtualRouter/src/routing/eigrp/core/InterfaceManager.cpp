// InterfaceTable.cpp

#include <VirtualRouter.h>

#include "InterfaceManager.h"
#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "interface/configs/InterfaceType.hpp"

namespace routing::eigrp
{
InterfaceManager::InterfaceManager(Eigrp& process) : process(process) {}

InterfaceManager::~InterfaceManager() {}

EigrpInterface* InterfaceManager::getInterface(interface::InterfaceKey key)
{
    if (auto it = eigrpInterfaceList.find(key); it != eigrpInterfaceList.end())
        return &it->second;
    return nullptr;
}

void InterfaceManager::tryCreateInterface(interface::Interface& interface)
{
    interface::InterfaceKey key = interface.configs.key;
    if (eigrpInterfaceList.find(key) != eigrpInterfaceList.end())
        return;
    auto ifaces = process.getConfigs().get<config::Eigrp::AF_INTERFACE>();
    auto reg = ifaces.find(interface.configs.key);
    if (reg != ifaces.end()) createInterface(interface.configs.key, *reg->second);
}

EigrpInterface* InterfaceManager::createInterface(interface::InterfaceKey key, config::EigrpInterfaceRegistry& cfg)
{
    if (eigrpInterfaceList.find(key) != eigrpInterfaceList.end())
        return nullptr;

    interface::Interface* iface = process.routingInstance->getInterfaceManager().get(key);
    if (!iface)
        return nullptr;

    types::AddressFamily af = process.addressFamily;
    if (af != types::AddressFamily::IPv4 && af != types::AddressFamily::IPv6)
        return nullptr;

    auto [it, ok] = eigrpInterfaceList.try_emplace(key, process, cfg, *iface);
    if (!ok)
        return nullptr;
    EigrpInterface* eigrpIfacePtr = &it->second;
    process.getTopology().synchronizeConnected(*eigrpIfacePtr);
    return eigrpIfacePtr;
}

bool InterfaceManager::destroyInterface(interface::InterfaceKey key)
{
    return eigrpInterfaceList.erase(key) != 0;
}

void InterfaceManager::deactivateAll()
{
    eigrpInterfaceList.clear();
}
} // namespace routing
