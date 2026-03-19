// InterfaceManager.cpp

#include <VirtualRouter.h>
#include <Global.h>

#include "InterfaceManager.h"
#include "ospf/area/Area.h"
#include "OspfInterface.h"
#include "ospf/OspfProcess.h"
#include "interface/Interface.h"

namespace OSPF
{
InterfaceManager::InterfaceManager(OspfProcess& p) : process(p) {}

InterfaceManager::~InterfaceManager() {}

OspfInterface* InterfaceManager::getInterface(const OspfInterfaceId& id)
{
    if (auto it = ospfInterfaceList.find(id); it != ospfInterfaceList.end())
        return &it->second;
    return nullptr;
}

OspfInterface* InterfaceManager::getInterfaceByAddress(const IPAddress& addr)
{
    for (auto& [id, iface] : ospfInterfaceList)
        if (iface.interfaceAddress.addr == addr.raw)
            return &iface;
    return nullptr;
}

std::vector<IPAddress> InterfaceManager::getReachableInterfaces(uint32_t area)
{
    std::vector<IPAddress> addrs;
    for (auto& [id, iface] : ospfInterfaceList)
        if (id.area == area)
            addrs.push_back(iface.interfaceAddress);
    return addrs;
}

bool InterfaceManager::isInterfaceReachable(uint32_t area, uint32_t ifaceId)
{
    for (auto& [id, iface] : ospfInterfaceList)
        if (id.area == area && iface.interfaceId == ifaceId)
            return true;
    return false;
}

OspfInterface& InterfaceManager::createInterface(Interface& interface, const OspfInterfaceId& key)
{
    if (auto it = ospfInterfaceList.find(key); it != ospfInterfaceList.end())
        return it->second;

    AddressFamily af = process.getAF();
    uint32_t id = process.getProcId();

    // TODO make a real config creation mechanism
    __uint128_t configKey = Config::generateOspfAfInterfaceKey(key.interfaceId, process.getProcId(), process.getAF(), process.isV3);
    Config::Reference<Config::OspfInterfaceBaseRegistry> configs =
        process.routingInstance->getGlobal().registry.create<Config::OspfInterfaceBaseRegistry>(configKey);
    process.routingInstance->getGlobal().registry.emplace(configs->get<Config::OspfInterfaceBase::BASE>(), configKey);

    if (!process.isV3)
    {
        auto ifaceIt = ospfInterfaceList.try_emplace(key, process, interface, configs, key);
        OspfInterface& ospfIface = ifaceIt.first->second;
        ospfIface.getArea().getOriginator().updateInterface(key.interfaceId);
        interface.ospfInterfaceList[id].IPv4 = &ospfIface;
        return ospfIface;
    }
    else
    {
        auto ifaceIt = ospfInterfaceList.try_emplace(key, process, interface, configs, key);
        OspfInterface& ospfIface = ifaceIt.first->second;
        ospfIface.getArea().getOriginator().updateInterface(key.interfaceId);
        if (af == AddressFamily::IPv4)
            interface.ospfInterfaceList[id].IPv4 = &ospfIface;
        else
            interface.ospfInterfaceList[id].IPv6 = &ospfIface;
        return ospfIface;
    }
}

void InterfaceManager::removeInterface(const OspfInterfaceId& id)
{
    // TODO: remove interface here
}

void InterfaceManager::refreshInterfaceList()
{
    // If OspfInterfaceId is 0, then its assumed that this interface exists.
    std::vector<std::pair<OspfInterfaceId, void*>> interfacesToProcess;

    if (process.getRouterId() == 0)
        if (!process.calculateRID()) return; // No valid RID

    {
        std::vector<std::unordered_map<OspfInterfaceId, OspfInterface>::node_type> interfacesToRemove;

        // Lock global interface state
        std::shared_lock<std::shared_mutex> sysLock(process.routingInstance->interfaceMutex);

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

        auto isInNetworkRange = [&](IPv4Address ip) -> std::optional<uint32_t>
        {
            // Use first area defined that matches.
            std::optional<uint32_t> area{std::nullopt};
            process.getConfigs().get<Config::Ospf::NETWORKS>().withRead([&](const auto& networks) {
                for (const auto& [prefix, a] : networks)
                {
                    if (prefix.contains(ip))
                    {
                        area = a;
                        break;
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
                { auto pfx = interface->configs.ipv4.getPrimaryPrefix(); currentAddress = IPPrefix(pfx.addr, pfx.prefixLength); }
                auto area = isInNetworkRange(currentAddress.addr);
                if (area.has_value()) key.emplace(interface->configs.ipv4.getPrimaryAddress().addr, area.value());
            }
            else
            {
                { auto pfx = interface->configs.ipv6.getLocalPrefix(); currentAddress = IPPrefix(pfx.addr, pfx.prefixLength); }
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
    ospfInterfaceList.clear();
}

void InterfaceManager::syncNeighbors()
{
    for (auto& [_, iface] : ospfInterfaceList)
    {
        iface.getNTable().syncUnicast();
    }
}
}
