// InterfaceTable.cpp

#include <VirtualRouter.h>

#include "InterfaceManager.h"
#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/EigrpTypes.hpp"
#include "interface/Interface.h"

namespace Eigrp
{
InterfaceManager::InterfaceManager(Eigrp& base) : base(base) {}

InterfaceManager::~InterfaceManager() {}

EigrpInterface* InterfaceManager::getInterface(uint32_t key)
{
    if (auto it = eigrpInterfaceList.find(key); it != eigrpInterfaceList.end())
        return &it->second;
    return nullptr;
}

EigrpInterface* InterfaceManager::createInterface(Interface* interface)
{
    if (auto it = eigrpInterfaceList.find(interface->configs.key); it != eigrpInterfaceList.end())
        return &it->second;

    if (interface)
    {
        // Add the interface to eigrp even if its down
        AddressFamily af = base.getAF();
        uint32_t as = base.getAS();

        EigrpConfigs::InterfaceConfigs* intConfig;
        if (base.isNamed())
        {
            auto configIt = eigrpInterfaceConfigList.find(interface->configs.key);
            if (configIt == eigrpInterfaceConfigList.end())
            {
                auto newConfig = eigrpInterfaceConfigList.emplace(interface->configs.key, interface->configs.key);
                intConfig = &newConfig.first->second;
            }
            else intConfig = &configIt->second;
        }
        else
        {
            intConfig = interface->getEigrpConfig(as, af, false);
        }

        if (af == AddressFamily::IPv4)
        {
            auto ifaceIt = eigrpInterfaceList.try_emplace(interface->configs.key, base, *intConfig, *interface);
            EigrpInterface* eigrpIfacePtr = &ifaceIt.first->second;
            base.getTopology().synchronizeConnected(*eigrpIfacePtr);
            interface->eigrpInterfaceList[as].IPv4 = eigrpIfacePtr;
            return eigrpIfacePtr;
        }
        else if (af == AddressFamily::IPv6)
        {
            auto ifaceIt = eigrpInterfaceList.try_emplace(interface->configs.key, base, *intConfig, *interface);
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
        std::vector<std::map<uint32_t, EigrpInterface>::node_type> interfacesToRemove; // Will clear when out of scope

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

        for (const auto& [id, interface] : base.routingInstance->interfaceList)
        {
            if (!interface || interface->shutdownFlag.load(std::memory_order_relaxed))
                continue;

            auto& ipInfo = interface->configs;
            bool inRange = false;
            bool remake = false;

            auto it = eigrpInterfaceList.find(id);

            if (base.getAF() == AddressFamily::IPv4)
            {
                inRange = config.isInNetworkRange(ipInfo.ipv4.getPrimaryAddress());
                // Compare known addresses
                if (it != eigrpInterfaceList.end())
                    remake = inRange && !ipInfo.ipv4.comparePrimaryAddress(IPv4Address(it->second.ifaceAddress.v4()));
            }
            else
            {
                bool ipv6Contained = false;
                if (isNamed)
                    ipv6Contained = eigrpInterfaceConfigList.contains(ipInfo.key) &&
                                    !eigrpInterfaceConfigList.at(ipInfo.key).shutdown;
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
            else if (!exists)
            {
                auto node = eigrpInterfaceList.extract(it);
                interfacesToRemove.push_back(std::move(node));
                it = eigrpInterfaceList.find(id);
            }
        }
    }

    for (auto& [exists, interface] : interfacesToProcess)
    {
        if (exists)
            base.getTopology().synchronizeConnected(*(static_cast<EigrpInterface*>(interface)));
        else
            createInterface(static_cast<Interface*>(interface));
    }
}

void InterfaceManager::deactivateAll()
{
    eigrpInterfaceList.clear();
}
}
