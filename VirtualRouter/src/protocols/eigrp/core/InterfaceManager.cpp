// InterfaceTable.cpp

#include "InterfaceManager.h"
#include "Eigrp.h"
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <Functions.h>
#include <Interface.h>
#include <InterfaceConfigs.h>

namespace Eigrp
{
InterfaceManager::InterfaceManager(Eigrp& base) : base(base) {}

InterfaceManager::~InterfaceManager() {}

EigrpInterface* InterfaceManager::getInterface(uint32_t key)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
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
        auto pairIt = eigrpInterfaceConfigList.find(interface->configs.key);
        if (pairIt != eigrpInterfaceConfigList.end())
        {
            intConfig = pairIt->second;
        }
        else
        {
            // INITIALIZE EIGRP CONFIGURATIONS
            intConfig = base.isNamed()
                ? new EigrpConfigs::InterfaceConfigs(interface->configs.key)
                : interface->getEigrpConfig(as, af, false);
            eigrpInterfaceConfigList[interface->configs.key] = intConfig;
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
    std::vector<std::map<uint32_t, EigrpInterface>::node_type> interfacesToRemove; // Will clear when out of scope

    if (base.routerID() == 0)
        base.calculateRID();

    {
        // Lock global interface state
        std::shared_lock<std::shared_mutex> sysLock(base.routingInstance->interfaceMutex);
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);

        // Remove shutdown interfaces
        for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
        {
            if (!it->second.getIface() || it->second.getIface()->shutdownFlag.load(std::memory_order_relaxed))
            {
                interfacesToRemove.push_back(eigrpInterfaceList.extract(it++));
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

            uint8_t ipAddress[4];
            bool ipv6Contained = false;
            auto& ipInfo = interface->configs;

            if (isNamed)
                ipv6Contained = eigrpInterfaceConfigList.contains(ipInfo.key) &&
                                !eigrpInterfaceConfigList[ipInfo.key]->shutdown;

            ipInfo.ipv4.getAddress(ipAddress);
            if (!ipv6Contained)
                ipv6Contained = ipInfo.eigrp.ipv6AutonomousSystems.contains(as) &&
                                interface->routingInstance == base.routingInstance;

            bool inRange = config.isInNetworkRange(ipAddress) || ipv6Contained;
            auto it = eigrpInterfaceList.find(id);

            if (inRange)
            {
                bool exists = it != eigrpInterfaceList.end();
                if (exists)
                    interfacesToProcess.push_back({exists, &it->second});
                else
                    interfacesToProcess.push_back({exists, interface});
            }
            else if (it != eigrpInterfaceList.end())
            {
                interfacesToRemove.push_back(eigrpInterfaceList.extract(it));
            }
        }
    }

    for (auto& [exists, interface] : interfacesToProcess)
    {
        if (exists)
            base.getTopology().synchronizeConnected(*(reinterpret_cast<EigrpInterface*>(interface)));
        else
            createInterface(reinterpret_cast<Interface*>(interface));
    }
}

void InterfaceManager::deactivateAll()
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    eigrpInterfaceList.clear();
}
}
