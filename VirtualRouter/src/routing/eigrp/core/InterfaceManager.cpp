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
InterfaceManager::InterfaceManager(Eigrp& base) : base(base) {}

InterfaceManager::~InterfaceManager() {}

EigrpInterface* InterfaceManager::getInterface(interface::InterfaceKey key)
{
    if (auto it = eigrpInterfaceList.find(key); it != eigrpInterfaceList.end())
        return &it->second;
    return nullptr;
}

config::Reference<config::EigrpInterfaceRegistry> InterfaceManager::getRegistryByKey(interface::InterfaceKey key)
{
    auto& registry = base.routingInstance->getRegistry();
    auto& configList = base.getGlobalConfigMgr().getConfigs().get<config::Eigrp::AF_INTERFACE>();
    return registry.emplaceBack(configList, key);
}

config::Reference<config::EigrpInterfaceRegistry> InterfaceManager::getRegistry(interface::Interface& iface)
{
    interface::InterfaceKey key = iface.configs.key;
    auto& registry = base.routingInstance->getRegistry();

    if (base.isNamed())
    {
        auto& configList = base.getGlobalConfigMgr().getConfigs().get<config::Eigrp::AF_INTERFACE>();
        return registry.emplaceBack(configList, key);
    }
    else
    {
        return iface.getEigrpConfig(base.getAS());
    }
}

EigrpInterface* InterfaceManager::createInterface(interface::Interface* interface)
{
    if (!interface)
        return nullptr;

    if (auto it = eigrpInterfaceList.find(interface->configs.key); it != eigrpInterfaceList.end())
        return &it->second;

    {
        // Add the interface to eigrp even if its down
        types::AddressFamily af = base.getAF();
        uint32_t as = base.getAS();
        interface::InterfaceKey key = interface->configs.key;

        // Get or create registry entry for this interface
        config::Reference<config::EigrpInterfaceRegistry> ifaceReg = getRegistry(*interface);

        if (af == types::AddressFamily::IPv4)
        {
            auto ifaceIt = eigrpInterfaceList.try_emplace(key, base, ifaceReg, *interface);
            EigrpInterface* eigrpIfacePtr = &ifaceIt.first->second;
            base.getTopology().synchronizeConnected(*eigrpIfacePtr);
            interface->eigrpInterfaceList[as].IPv4 = eigrpIfacePtr;
            return eigrpIfacePtr;
        }
        else if (af == types::AddressFamily::IPv6)
        {
            auto ifaceIt = eigrpInterfaceList.try_emplace(key, base, ifaceReg, *interface);
            EigrpInterface* eigrpIfacePtr = &ifaceIt.first->second;
            base.getTopology().synchronizeConnected(*eigrpIfacePtr);
            interface->eigrpInterfaceList[as].IPv6 = eigrpIfacePtr;
            return eigrpIfacePtr;
        }
    }
    return nullptr;
}

void InterfaceManager::refreshInterfaceList()
{
    std::vector<std::pair<bool, void*>> interfacesToProcess;

    if (base.routerID() == 0)
        if (!base.calculateRID()) return; // No valid RID

    {
        std::vector<std::unordered_map<interface::InterfaceKey, EigrpInterface>::node_type> interfacesToRemove; // Will clear when out of scope

        // Remove shutdown interfaces
        for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
        {
            if (!it->second.getIface() || it->second.getIface()->shutdownFlag.load(std::memory_order_relaxed))
            {
                auto node = eigrpInterfaceList.extract(it++);
                interfacesToRemove.push_back(std::move(node));
            }
            else
            {
                ++it;
            }
        }

        auto& config = base.getGlobalConfigMgr();
        bool isNamed = base.isNamed();
        uint32_t as = base.getAS();

        for (const auto& [id, interface] : base.routingInstance->getInterfaceManager().snapshot())
        {
            if (!interface || interface->shutdownFlag.load(std::memory_order_relaxed))
                continue;

            auto& ipInfo = interface->configs;
            bool inRange = false;
            bool remake = false;

            auto it = eigrpInterfaceList.find(id);

            if (base.getAF() == types::AddressFamily::IPv4)
            {
                inRange = config.isInNetworkRange(ipInfo.ipv4.getPrimaryAddress());
                // Compare known addresses
                if (it != eigrpInterfaceList.end())
                    remake = inRange && !ipInfo.ipv4.comparePrimaryAddress(types::IPv4Address(it->second.ifaceAddress.v4()));
            }
            else
            {
                bool ipv6Contained = false;
                if (isNamed)
                {
                    auto& afIfaces = base.getGlobalConfigMgr().getConfigs().get<config::Eigrp::AF_INTERFACE>();
                    auto regIt = afIfaces.find(ipInfo.key);
                    ipv6Contained = (regIt != afIfaces.end()) && !regIt->second.get().get<config::EigrpInterface::SHUTDOWN>().load();
                }
                if (!ipv6Contained)
                    ipv6Contained = ipInfo.eigrp.ipv6AutonomousSystems.contains(as) &&
                                    interface->getVRF() == base.routingInstance;
                inRange = ipv6Contained;
                // Compare known addresses
                if (it != eigrpInterfaceList.end())
                    remake = inRange && ipInfo.ipv6.getLocalAddress().addr != it->second.ifaceAddress.v6();
            }

            if (remake)
            {
                auto node = eigrpInterfaceList.extract(it);
                interfacesToRemove.push_back(std::move(node));
                it = eigrpInterfaceList.find(id);
            }

            bool exists = eigrpInterfaceList.contains(id);
            if (inRange)
            {
                if (exists)
                    interfacesToProcess.push_back({true, &it->second});
                else
                    interfacesToProcess.push_back({false, interface});
            }
            else if (exists)
            {
                auto node = eigrpInterfaceList.extract(it);
                interfacesToRemove.push_back(std::move(node));
            }
        }
    }

    for (auto& [exists, interface] : interfacesToProcess)
    {
        if (exists)
            base.getTopology().synchronizeConnected(*(static_cast<EigrpInterface*>(interface)));
        else
            createInterface(static_cast<interface::Interface*>(interface));
    }
}

void InterfaceManager::deactivateAll()
{
    eigrpInterfaceList.clear();
}
} // namespace routing
