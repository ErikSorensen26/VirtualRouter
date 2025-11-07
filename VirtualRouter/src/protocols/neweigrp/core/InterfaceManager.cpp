// InterfaceTable.cpp

#include "InterfaceManager.h"
#include "EigrpCore.h"
#include <EigrpInterface.h>
#include <VirtualRouter.h>
#include <Interface.h>

namespace Eigrp
{
InterfaceManager::InterfaceManager(Eigrp& base) : base(base) {}

InterfaceManager::~InterfaceManager()
{
    std::unique_lock<std::shared_mutex> lock(interfaceMutex);
    for (auto& [key, iface] : eigrpInterfaceList)
    {
        delete iface;
        iface = nullptr;
    }
    eigrpInterfaceList.clear();
}

EigrpInterface* InterfaceManager::createInterface(Interface* interface)
{
    if (eigrpInterfaceList.contains(interface->configs.key))
        return eigrpInterfaceList[interface->configs.key];

    if (interface)
    {
        // Add the interface to eigrp even if its down
        InterfaceConfigs& interfaceInfo = interface->configs;
        uint8_t ipAddress[16];

        AddressFamily af = base.getAF();
        uint32_t as = base.getAS();

        if (af == AddressFamily::IPv4)
            interfaceInfo.ipv4.getAddress(ipAddress);
        else
            if (!interfaceInfo.ipv6.getGlobalUnicast(ipAddress)) return nullptr;

        EigrpInterface* instance = nullptr;
        EigrpInterfaceInstance* interfaceInstance = nullptr;

        if (interface->eigrpInterfaceList.find(as) == interface->eigrpInterfaceList.end() || (!interface->eigrpInterfaceList.find(as)->second))
        {
            interfaceInstance = new EigrpInterfaceInstance();
            interface->eigrpInterfaceList[as] = interfaceInstance;
        }
        else
        {
            interfaceInstance = interface->eigrpInterfaceList[as];
        }

        EigrpConfigs::InterfaceConfigs* intConfig;
        auto pairIt = eigrpInterfaceConfigList.find(interface->configs.key);
        if (pairIt != eigrpInterfaceConfigList.end())
        {
            intConfig = pairIt->second;
        }
        else
        {
            // INITIALIZE EIGRP CONFIGURATIONS
            if (base.isNamed())
            {
                intConfig = new EigrpConfigs::InterfaceConfigs(interface->configs.key);
            }
            else
            {
                intConfig = interface->getEigrpConfig(as, af, false);
            }
            eigrpInterfaceConfigList[interface->configs.key] = intConfig;
        }

        instance = new EigrpInterface(base, *pairIt->second, *interface);

        if (af == AddressFamily::IPv4)
        {
            interfaceInstance->IPv4 = instance;
            eigrpInterfaceList[interface->configs.key] = instance;
            interface->eigrpInterfaceList[as] = interfaceInstance;
            return instance;
        }
        else if (af == AddressFamily::IPv6)
        {
            interfaceInstance->IPv6 = instance;
            eigrpInterfaceList[interface->configs.key] = instance;
            interface->eigrpInterfaceList[as] = interfaceInstance;
            return instance;
        }
    }
    return nullptr;
}

void InterfaceManager::refreshInterfaceList()
{ 
    {
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);
        // Validate existing interfaces
        for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
        {
            if (it->second && it->second->getIface() && it->second->getIface()->shutdownFlag.load(std::memory_order_relaxed))
            {
                delete it->second;
                it = eigrpInterfaceList.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    auto& config = base.getGlobalConfigMgr();
    bool autoSum = config.isAutoSummarized();
    bool isNamed = base.isNamed();
    AddressFamily af = base.getAF();
    uint32_t as = base.getAS();

    // Iterate through all interfaces
    {
        std::unique_lock<std::shared_mutex> lock(base.routingInstance->interfaceMutex);
        for (const auto& [id, interface] : base.routingInstance->interfaceList)
        {
            // Add the interface as existing
            if (interface && !interface->shutdownFlag.load(std::memory_order_relaxed))
            {
                uint8_t ipAddress[16];
                bool ipv6Contained = false;
                auto& ipInfo = interface->configs;

                if (isNamed)
                {
                    std::shared_lock<std::shared_mutex> confLock(interfaceMutex);
                    ipv6Contained = eigrpInterfaceConfigList.contains(ipInfo.key) && !eigrpInterfaceConfigList[ipInfo.key]->shutdown;
                }

                ipInfo.ipv4.getAddress(ipAddress);
                if (!ipv6Contained)
                {
                    ipv6Contained = ipInfo.eigrp.ipv6AutonomousSystems.contains(base.getAS()) &&
                        interface->routingInstance == base.routingInstance;
                }

                // Test the address and add the interface if approved
                if (config.isInNetworkRange(ipAddress) || ipv6Contained)
                {
                    // Add interface to eigrp
                    std::unique_lock<std::shared_mutex> interfaceLock(interfaceMutex);
                    auto eigrpInterfaceIt = interface->eigrpInterfaceList.find(as);
                    if (eigrpInterfaceIt == interface->eigrpInterfaceList.end() || 
                        eigrpInterfaceList.find(id) == eigrpInterfaceList.end())
                    {
                        auto* newInterface = createInterface(interface);
                        if (!newInterface)
                        {
                            continue;
                        }

                        if (autoSum && af != AddressFamily::IPv6)
                        {
                            uint8_t majorNetwork[4];
                            uint8_t defaultMask = Functions::findClassfullNetworkAndMask(majorNetwork, ipAddress);
                            IPPrefix pref = { majorNetwork, defaultMask, af };

                            // Only summarize if the interface is in a different major network
                            auto& aggregator = newInterface->getAggregator();
                            if (!aggregator.isSummarized(pref))
                            {
                                aggregator.installSummary(pref, true);
                            }
                        }
                    }
                }
                else
                {
                    // Check and remove interface from eigrp if no eigrp neig
                    std::shared_lock<std::shared_mutex> interfaceLost(interfaceMutex);
                    auto intIt = eigrpInterfaceList.find(id);
                    
                    if (intIt != eigrpInterfaceList.end())
                    {
                        // Delete interface if no static neighbors are found.
                        delete eigrpInterfaceList[id];
                        eigrpInterfaceList[id] = nullptr;
                        eigrpInterfaceList.erase(id);
                    }
                }
            }
            else
            {
                // Remove shutdown interface
                if (eigrpInterfaceList.find(id) != eigrpInterfaceList.end())
                {
                    delete eigrpInterfaceList[id];
                    eigrpInterfaceList[id] = nullptr;
                    eigrpInterfaceList.erase(id);
                }
            }
        };
    }
    updateRoutingTableForConnected(); //TODO
}

void InterfaceManager::removeInterface(uint32_t key)
{
    std::unique_lock<std::shared_mutex> lock(interfaceMutex);
    auto it = eigrpInterfaceList.find(key);
    if (it == eigrpInterfaceList.end()) return;

    EigrpInterface* iface = it->second;
    delete iface;
    eigrpInterfaceList.erase(it);
    eigrpInterfaceConfigList.erase(key);
}

void InterfaceManager::deactivateAll()
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
    {
        // Send termination message
        delete it->second;
        it->second = nullptr;
        it = eigrpInterfaceList.erase(it);
    }
    eigrpInterfaceList.clear();
    if (base.isNamed())
    {
        for (auto it = eigrpInterfaceConfigList.begin(); it != eigrpInterfaceConfigList.end();)
        {
            // Delete configs
            delete it->second;
            it->second = nullptr;
            it = eigrpInterfaceConfigList.erase(it);
        }
        eigrpInterfaceConfigList.clear();
    }
}
}
