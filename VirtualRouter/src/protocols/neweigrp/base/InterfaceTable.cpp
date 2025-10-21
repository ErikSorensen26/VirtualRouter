// InterfaceTable.cpp

#include "InterfaceTable.h"

namespace Protocol
{
EigrpInterface* InterfaceTable::createInterface(Interface* interface)
{
    if (interface)
    {
        // Add the interface to eigrp even if its down
        InterfaceConfigs& interfaceInfo = interface->configs;
        uint8_t ipAddress[16];

        if (addressFamily == AddressFamily::IPv4)
            interfaceInfo.ipv4.getAddress(ipAddress);
        else
            if (!interfaceInfo.ipv6.getGlobalUnicast(ipAddress)) return nullptr;

        EigrpInterface* instance = nullptr;
        EigrpInterfaceInstance* interfaceInstance = nullptr;

        if (interface->eigrpInterfaceList.find(asNumber) == interface->eigrpInterfaceList.end() || (!interface->eigrpInterfaceList.find(asNumber)->second))
        {
            interfaceInstance = new EigrpInterfaceInstance();
            interface->eigrpInterfaceList[asNumber] = interfaceInstance;
        }
        else
        {
            interfaceInstance = interface->eigrpInterfaceList[asNumber];
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
            if (namedMode)
            {
                intConfig = new EigrpConfigs::InterfaceConfigs(interface->configs.key);
            }
            else
            {
                intConfig = interface->getEigrpConfig(asNumber, addressFamily, false);
            }
            eigrpInterfaceConfigList[interface->configs.key] = intConfig;
        }

        if (addressFamily == AddressFamily::IPv4)
        {
            instance = new EigrpInterface(*this, eigrpInterfaceConfigList[interface->configs.key], interface);
            interfaceInstance->IPv4 = instance;
            eigrpInterfaceList[interface->configs.key] = instance;
            interface->eigrpInterfaceList[asNumber] = interfaceInstance;
            return instance;
        }
        else if (addressFamily == AddressFamily::IPv6)
        {
            instance = new EigrpInterface(*this, intConfig, interface);
            interfaceInstance->IPv6 = instance;
            eigrpInterfaceList[interface->configs.key] = instance;
            return instance;
        }
    }
    return nullptr;
}

void Eigrp::refreshInterfaceList()
{ 
    {
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);
        // Validate existing interfaces
        for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
        {
            if (it->second && it->second->currentInterface && it->second->currentInterface->shutdownFlag.load(std::memory_order_relaxed))
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

    // Iterate through all interfaces
    {
        std::unique_lock<std::shared_mutex> lock(routingInstance->interfaceMutex);
        for (const auto& [id, interface] : routingInstance->interfaceList)
        {
            // Add the interface as existing
            if (interface && !interface->shutdownFlag.load(std::memory_order_relaxed))
            {
                uint8_t ipAddress[16];
                bool ipv6Contained = false;
                auto& ipInfo = interface->configs;

                if (namedMode)
                {
                    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                    ipv6Contained = eigrpInterfaceConfigList.contains(ipInfo.key) && !eigrpInterfaceConfigList[ipInfo.key]->shutdown;
                }

                ipInfo.ipv4.getAddress(ipAddress);
                if (!ipv6Contained)
                {
                    ipv6Contained = ipInfo.eigrp.ipv6AutonomousSystems.contains(asNumber) &&
                        interface->routingInstance == routingInstance;
                }

                // Test the address and add the interface if approved
                if (testAddress(ipAddress) || ipv6Contained)
                {
                    // Add interface to eigrp
                    std::unique_lock<std::shared_mutex> interfaceLock(interfaceMutex);
                    auto eigrpInterfaceIt = interface->eigrpInterfaceList.find(asNumber);
                    if (eigrpInterfaceIt == interface->eigrpInterfaceList.end() || 
                        eigrpInterfaceList.find(id) == eigrpInterfaceList.end())
                    {
                        auto* newInterface = addEigrpInterface(interface);
                        if (!newInterface)
                        {
                            continue;
                        }

                        if (configs.autoSummarizationEnabled.load(std::memory_order_relaxed) && addressFamily != AddressFamily::IPv6)
                        {
                            uint8_t majorNetwork[4];
                            uint8_t defaultMask = Functions::findClassfullNetworkAndMask(majorNetwork, ipAddress);

                            // Only summarize if the interface is in a different major network
                            if (!newInterface->isRouteSummarized(majorNetwork, defaultMask))
                            {
                                newInterface->addSummaryRoute(majorNetwork, defaultMask, true);
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
    updateRoutingTableForConnected();
}

void EigrpInterfaceManager::deactivateAll()
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    for (auto it = interfaceMgr.eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
    {
        // Send termination message
        delete it->second;
        it->second = nullptr;
        it = eigrpInterfaceList.erase(it);
    }
    eigrpInterfaceList.clear();
    if (namedMode)
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

void EigrpInterfaceManager::enableUnicastPeer(const IPAddress& neighborIp, uint32_t key)
{
    // Add unicast neighbor to the unicast neighbor list
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        configs.unicastNeighbors[key].emplace(neighborIp);
    }

    // Find the interface to add the neighbor
    {
        std::shared_lock<std::shared_mutex> intLock(interfaceMutex);
        auto intIt = eigrpInterfaceList.find(key);
        if (intIt != eigrpInterfaceList.end())
        {
            intIt->second->addUnicastNeighbor(neighborIp);
        }
    }
}

void EigrpInterfaceManager::disableUnicastPeer(const IPAddress& neighborIp, uint32_t key)
{
    // Remove unicast neighbor from the unicast neighbor list
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        configs.unicastNeighbors[key].erase(neighborIp);
    }

    // Find the interface to remove the neighbor from
    {
        std::shared_lock<std::shared_mutex> intLock(interfaceMutex);
        auto intIt = eigrpInterfaceList.find(key);
        if (intIt != eigrpInterfaceList.end())
        {
            intIt->second->removeUnicastNeighbor(neighborIp);
        }
    }
}
}
