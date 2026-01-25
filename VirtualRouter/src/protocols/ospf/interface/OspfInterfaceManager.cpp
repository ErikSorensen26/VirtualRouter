// OspfInterfaceManager.cpp

#include "OspfInterfaceManager.h"
#include <OspfTopology.h>
#include "OspfInterface.h"
#include <OspfProcess.h>
#include <Functions.h>
#include <Interface.h>
#include <OspfTypes.hpp>
#include <VirtualRouter.h>
#include <OspfArea.h>

namespace OSPF
{
InterfaceManager::InterfaceManager(OspfProcess& p) : process(p) {}

InterfaceManager::~InterfaceManager() {}

OspfInterface* InterfaceManager::getInterface(const OspfInterfaceId& id)
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    if (auto it = ospfInterfaceList.find(id); it != ospfInterfaceList.end())
        return &it->second;
    return nullptr;
}

OspfInterface& InterfaceManager::createInterface(Interface& interface, OspfInterfaceId& key)
{
    if (auto it = ospfInterfaceList.find(key); it != ospfInterfaceList.end())
        return it->second;

    AddressFamily af = process.getAF();
    uint32_t id = process.getProcId();

    InterfaceConfigs& intConfig = interface.getOspfConfig(id, af);

    if (!process.isV3)
    {
        auto ifaceIt = ospfInterfaceList.try_emplace(key, process, interface, intConfig, key);
        OspfInterface& ospfIface = ifaceIt.first->second;
        ospfIface.getArea().getOriginator().updateInterface(key.interfaceId);
        interface.ospfInterfaceList[id].IPv4 = &ospfIface;
        return ospfIface;
    }
    else
    {
        auto ifaceIt = ospfInterfaceList.try_emplace(key, process, interface, intConfig, key);
        OspfInterface& ospfIface = ifaceIt.first->second;
        ospfIface.getArea().getOriginator().updateInterface(key.interfaceId);
        if (af == AddressFamily::IPv4)
            interface.ospfInterfaceList[id].IPv4 = &ospfIface;
        else
            interface.ospfInterfaceList[id].IPv6 = &ospfIface;
        return ospfIface;
    }
}

void InterfaceManager::refreshInterfaceList()
{
    // TODO: eventually clear out unused areas.

    // If OspfInterfaceId is 0, then its assumed that this interface exists.
    std::vector<std::pair<OspfInterfaceId, void*>> interfacesToProcess;

    if (process.getRouterId() == 0)
        if (!process.calculateRID()) return; // No valid RID

    {
        std::vector<std::map<OspfInterfaceId, OspfInterface>::node_type> interfacesToRemove;

        // Lock global interface/topology state
        std::shared_lock<std::shared_mutex> sysLock(process.routingInstance->interfaceMutex);
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);
        std::shared_lock<std::shared_mutex> topoLock(process.topologyMu);

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

        uint32_t procId = process.getProcId();

        auto isInNetworkRange = [&](uint8_t tid, uint8_t* ip) -> std::optional<uint32_t>
        {
            // Use first area defined that matches.
            auto& topology = process.insureTopology(tid);
            topology.getConfigs().get<Config::OspfTopology::NETWORKS>().withRead([&](const auto& net) {
                for (const auto& [prefix, area] : net)
                {
                    if (Functions::compareNetworkWithIp(prefix.addr, ip, prefix.prefixLength, AddressFamily::IPv4))
                    {
                        return area;
                    }
                }
            });
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
                currentAddress = interface->configs.ipv4.getPrimaryPrefix();
                auto area = isInNetworkRange(interface->configs.tid.load(std::memory_order_relaxed), currentAddress.addr);
                if (area.has_value()) key.emplace(interface->configs.ipv4.getPrimaryAddress(), area.value());
            }
            else
            {
                currentAddress = interface->configs.ipv6.getLocalPrefix();
                bool inRange = ipInfo.ospf.enabledProcesses.contains(procId) &&
                                 interface->getVRF() == process.routingInstance;
                if (inRange) key.emplace(id, ipInfo.ospf.enabledProcesses[procId]);
            }

            // Remove or change any interfaces
            for (auto it = ospfInterfaceList.begin(); it != ospfInterfaceList.end();)
            {
                if (it->second.interfaceId == id)
                {
                    uint8_t tid = interface->configs.tid.load(std::memory_order_relaxed);
                    if (!key.has_value() || it->first.area != key.value().area ||
                        it->second.interfaceAddress != currentAddress)
                    {
                        auto node = ospfInterfaceList.extract(it);
                        interfacesToRemove.push_back(std::move(node));
                    }
                    // Change topology/area (OSPFv2 MTR ONLY)
                    else if (!process.isV3 && it->second.topology.load(std::memory_order_relaxed)->tid != tid)
                    {
                        auto& topology = process.insureTopology(tid);
                        it->second.topology.store(&topology, std::memory_order_relaxed);
                        it->second.area.store(&topology.insureArea(key->area), std::memory_order_relaxed);
                    }
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
        {
            auto* iface = static_cast<OspfInterface*>(interface);
            iface->getArea().getOriginator().updateInterface(iface->id.interfaceId);
        }
        else
            createInterface(*static_cast<Interface*>(interface), key);
    }
}

void InterfaceManager::deactivateAll()
{
    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
    ospfInterfaceList.clear();
}
}
