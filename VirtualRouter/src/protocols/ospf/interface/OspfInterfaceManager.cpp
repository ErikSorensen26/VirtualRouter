// OspfInterfaceManager.cpp

#include "OspfInterfaceManager.h"
#include "OspfInterface.h"
#include <OspfProcess.h>
#include <Functions.h>
#include <Interface.h>
#include <OspfTypes.hpp>
#include <VirtualRouter.h>

namespace OSPF
{
InterfaceManager::InterfaceManager(OspfProcess& p) : process(p) {}

InterfaceManager::~InterfaceManager() {}

OspfInterface* InterfaceManager::getInterface(OspfInterfaceId& id)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (auto it = ospfInterfaceList.find(id); it != ospfInterfaceList.end())
        return &it->second;
    return nullptr;
}

OspfInterface* InterfaceManager::createInterface(Interface* interface, OspfInterfaceId& key)
{
    if (auto it = ospfInterfaceList.find(key); it != ospfInterfaceList.end())
        return &it->second;

    if (interface)
    {
        AddressFamily af = process.getAF();
        uint32_t id = process.getProcId();

        InterfaceConfigs* intConfig;
        auto pairIt = ospfInterfaceConfigList.find(key);
        if (pairIt != ospfInterfaceConfigList.end())
        {
            intConfig = pairIt->second;
        }
        else
        {
            // INITIALIZE OSPF CONFIGURATIONS
            intConfig = process.isV3
                ? new InterfaceConfigs(interface->configs.key)
                : interface->getOspfConfig(id, false);
            ospfInterfaceConfigList[key] = intConfig;
        }

        if (!process.isV3)
        {
            auto ifaceIt = ospfInterfaceList.try_emplace(key, process, *interface, key);
            OspfInterface* ospfIfacePtr = &ifaceIt.first->second;
            // TODO: Sync connected
            interface->ospfInterfaceList[id].IPv4 = ospfIfacePtr;
            return ospfIfacePtr;
        }
        else
        {
            auto ifaceIt = ospfInterfaceList.try_emplace(key, process, *interface, key);
            OspfInterface* ospfIfacePtr = &ifaceIt.first->second;
            // TODO: Sync connected
            if (af == AddressFamily::IPv4)
                interface->ospfInterfaceList[id].IPv4 = ospfIfacePtr;
            else
                interface->ospfInterfaceList[id].IPv6 = ospfIfacePtr;
            return ospfIfacePtr;
        }
    }
    return nullptr;
}

void InterfaceManager::refreshInterfaceList()
{
    // If OspfInterfaceId is 0, then its assumed that this interface exists.
    std::vector<std::pair<OspfInterfaceId, void*>> interfacesToProcess;

    if (process.getRouterId() == 0)
        if (!process.calculateRID()) return; // No valid RID

    {
        std::vector<std::map<OspfInterfaceId, OspfInterface>::node_type> interfacesToRemove;

        // Lock global interface state
        std::shared_lock<std::shared_mutex> sysLock(process.routingInstance->interfaceMutex);
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);

        // Remove shutdown interfaces
        for (auto it = ospfInterfaceList.begin(); it != ospfInterfaceList.end();)
        {
            if (it->second.getIface().shutdownFlag.load(std::memory_order_relaxed))
            {
                auto node = ospfInterfaceList.extract(it++);
                interfacesToRemove.push_back(std::move(node));
            }
            else
            {
                ++it;
            }
        }

        auto& config = process.getConfigs();
        uint32_t procId = process.getProcId();

        auto isInNetworkRange = [&](uint8_t* ip) -> std::optional<uint32_t>
        {
            // Use first area defined that matches.
            auto& configs = process.getConfigs();
            std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
            for (const auto& network : configs.networks)
            {
                if (Functions::compareNetworkWithIp(network.prefix.addr, ip, network.prefix.prefixLength, AddressFamily::IPv4))
                {
                    return network.area;
                }
            }
            return std::nullopt;
        };

        for (const auto& [id, interface] : process.routingInstance->interfaceList)
        {
            if (!interface || interface->shutdownFlag.load(std::memory_order_relaxed))
                continue;

            auto& ipInfo = interface->configs;

            IPPrefix currentAddress;
            std::optional<OspfInterfaceId> key;

            if (!process.isV3)
            {
                currentAddress = interface->configs.ipv4.getAddressMask();
                auto area = isInNetworkRange(currentAddress.addr);
                if (area.has_value()) key.emplace(id, area.value());
            }
            else
            {
                currentAddress = interface->configs.ipv6.getLocalPrefix();
                bool inRange = ipInfo.ospf.enabledProcesses.contains(procId) &&
                                 interface->getVRF() == process.routingInstance;
                if (inRange) key.emplace(id, ipInfo.ospf.enabledProcesses[procId]);
            }

            // Remove any invalid interfaces (wrong area or wrong ip)
            for (auto it = ospfInterfaceList.begin(); it != ospfInterfaceList.end();)
            {
                if (!key.has_value() || it->first.area != key.value().area ||
                    it->second.interfaceAddress != currentAddress)
                {
                    auto node = ospfInterfaceList.extract(it);
                    interfacesToRemove.push_back(std::move(node));
                }
            }

            if (!key.has_value()) continue;

            auto it = ospfInterfaceList.find(key.value());
            if (it != ospfInterfaceList.end())
                interfacesToProcess.push_back({{}, &it->second});
            else
                interfacesToProcess.push_back({key.value(), interface});
        }
    }

    for (auto& [key, interface] : interfacesToProcess)
    {
        // Check if this interface already exists.
        bool exists = key.interfaceId == 0 && key.area == 0;
        if (exists)
        {//TODO: syncronize connected
        }
        else
            createInterface(static_cast<Interface*>(interface), key);
    }
}

void InterfaceManager::deactivateAll()
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    ospfInterfaceList.clear();
}
}
