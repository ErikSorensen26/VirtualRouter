// InterfaceManager.cpp

#include <VirtualRouter.h>
#include <Global.h>

#include "InterfaceManager.h"
#include "ospf/area/Area.h"
#include "OspfInterface.h"
#include "ospf/OspfProcess.h"
#include "ospf/neighbor/NeighborTable.h"
#include "interface/Interface.h"
#include "ospf/transmission/PacketDispatcher.h"

namespace routing::ospf
{
InterfaceManager::InterfaceManager(OspfProcess& p) : process(p) {}

InterfaceManager::~InterfaceManager() {}

const OspfInterface* InterfaceManager::getInterface(const OspfInterfaceId& id) const
{
    if (auto it = ospfInterfaceList.find(id); it != ospfInterfaceList.end())
        return &it->second;
    return nullptr;
}

const OspfInterface* InterfaceManager::getInterfaceByAddress(const types::IPAddress& addr) const
{
    for (auto& [id, iface] : ospfInterfaceList)
        if (iface.interfaceAddress.addr == addr.raw)
            return &iface;
    return nullptr;
}

std::vector<types::IPAddress> InterfaceManager::getReachableInterfaces(uint32_t area)
{
    std::vector<types::IPAddress> addrs;
    for (auto& [id, iface] : ospfInterfaceList)
        if (id.area == area)
            addrs.push_back(iface.interfaceAddress);
    return addrs;
}

const config::OspfInterfaceBaseRegistry& InterfaceManager::getInterfaceBaseConfigs(const OspfInterface& iface) const
{
    return iface.baseConfigs;
}

const config::OspfInterfaceRegistry& InterfaceManager::getInterfaceConfigs(const OspfInterface& iface) const
{
    return iface.configs;
}

const NeighborTable& InterfaceManager::getNTable(const OspfInterface& iface) const
{
    return iface.ntable;
}

bool InterfaceManager::isInterfaceReachable(uint32_t area, uint32_t ifaceId)
{
    for (auto& [id, iface] : ospfInterfaceList)
        if (id.area == area && iface.interfaceId == ifaceId)
            return true;
    return false;
}

void InterfaceManager::broadcastLsu(Area& area, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    for (auto& [id, iface] : ospfInterfaceList)
    {
        if (id.area != area.areaId)
            continue;

        if (iface.configs.get<config::OspfInterface::DATABASE_FILTER>().load())
            continue;

        if (iface.configs.get<config::OspfInterface::NETWORK>().load() == config::ospf::NetworkType::BROADCAST)
        {
            if (iface.ntable.size() > 0)
                iface.dispatcher.sendReliableLsu(nullptr, records);
        }
        else
        {
            iface.ntable.forEach([&iface, &records](uint32_t, Neighbor& nbr) {
                if (nbr.getState() >= Neighbor::State::EXCHANGE)
                    iface.dispatcher.sendReliableLsu(&nbr, records);
            });
        }
    }
}

void InterfaceManager::runDCIntegrityScan(bool enabled)
{
    for (auto& [ifId, iface] : ospfInterfaceList)
    {
        if ((iface.configs.get<config::OspfInterface::DEMAND_CIRCUIT>().load() ||
            iface.configs.get<config::OspfInterface::FLOOD_REDUCTION>().load()) &&
            iface.floodReduction != enabled)
        {
            iface.floodReduction = enabled;

            // Re-announce the updated DC bit to neighbors and re-originate
            // this interface's router-LSA contribution with the new options.
            iface.tmgr.scheduleHello();
            iface.updateOriginations();
        }
    }
}

OspfInterface& InterfaceManager::createInterface(interface::Interface& interface, const OspfInterfaceId& key)
{
    if (auto it = ospfInterfaceList.find(key); it != ospfInterfaceList.end())
        return it->second;

    auto ifaceIt = ospfInterfaceList.try_emplace(key, process, interface, key);
    OspfInterface& ospfIface = ifaceIt.first->second;
    ospfIface.updateOriginations();
    return ospfIface;
}

void InterfaceManager::removeInterface(const OspfInterfaceId& id)
{
    auto it = ospfInterfaceList.find(id);
    if (it == ospfInterfaceList.end()) return;

    uint32_t areaId = id.area;
    ospfInterfaceList.erase(it); // destructor tears down neighbors, timers, LSAs

    // Auto-remove non-backbone areas that are now empty
    if (areaId != 0)
    {
        bool hasInterfaces = false;
        for (const auto& [ifaceId, iface] : ospfInterfaceList)
        {
            if (ifaceId.area == areaId)
            {
                hasInterfaces = true;
                break;
            }
        }
        if (!hasInterfaces)
            process.removeArea(areaId);
    }
}

void InterfaceManager::refreshInterfaceList()
{
    // If OspfInterfaceId is 0, then its assumed that this interface exists.
    std::vector<std::pair<OspfInterfaceId, void*>> interfacesToProcess;

    if (process.getRouterId() == 0)
        if (!process.calculateRID()) return; // No valid RID

    {
        std::vector<std::unordered_map<OspfInterfaceId, OspfInterface>::node_type> interfacesToRemove;

        // Remove shutdown interfaces
        for (auto it = ospfInterfaceList.begin(); it != ospfInterfaceList.end();)
        {
            if (it->second.iface.shutdownFlag.load(std::memory_order_relaxed))
            {
                auto node = ospfInterfaceList.extract(it++);
                interfacesToRemove.push_back(std::move(node));
            }
            else
            {
                ++it;
            }
        }

        uint32_t procId = process.procId;

        auto isInNetworkRange = [&](types::IPv4Address ip) -> std::optional<uint32_t>
        {
            // Use first area defined that matches.
            std::optional<uint32_t> area{std::nullopt};
            process.configs.get<config::Ospf::NETWORKS>().withRead([&](const auto& networksList) {
                for (const auto& networks : networksList)
                    for (const auto& [prefix, a] : networks)
                    {
                        if (prefix.contains(ip))
                        {
                            area = a;
                            break;
                        }
                    }
            });
            return area;
        };

        for (const auto& [id, interface] : process.routingInstance->getInterfaceManager().snapshot())
        {
            if (!interface || interface->shutdownFlag.load(std::memory_order_relaxed))
                continue;

            types::IPPrefix currentAddress;
            std::optional<OspfInterfaceId> key;

            if (!process.isV3)
            {
                { auto pfx = interface->configs.ipv4.getPrimaryPrefix(); currentAddress = types::IPPrefix(pfx.addr, pfx.prefixLength); }
                auto area = isInNetworkRange(static_cast<uint32_t>(currentAddress.addr));
                if (area.has_value()) key.emplace(interface->configs.ipv4.getPrimaryAddress().addr, area.value());
            }
            else if (process.af == types::AddressFamily::IPv4)
            {
                { auto pfx = interface->configs.ipv4.getPrimaryPrefix(); currentAddress = types::IPPrefix(pfx.addr, pfx.prefixLength); }
                auto area = isInNetworkRange(static_cast<uint32_t>(currentAddress.addr));
                if (area.has_value()) key.emplace(id.getId(), area.value());
            }
            else
            {
                { auto pfx = interface->configs.ipv6.getLocalPrefix(); currentAddress = types::IPPrefix(pfx.addr, pfx.prefixLength); }
                (void)procId;
                bool inRange = false; // OSPFv3 interface membership managed elsewhere
                if (inRange) key.emplace(id.getId(), 0);
            }

            // Remove any stale entries for this hardware interface (wrong area or wrong IP)
            for (auto it = ospfInterfaceList.begin(); it != ospfInterfaceList.end();)
            {
                if (&it->second.iface != interface)
                {
                    ++it;
                    continue;
                }
                if (!key.has_value() || it->first.area != key.value().area ||
                    it->second.interfaceAddress != currentAddress)
                {
                    auto node = ospfInterfaceList.extract(it++);
                    interfacesToRemove.push_back(std::move(node));
                }
                else
                {
                    ++it;
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
            iface->updateOriginations();
        }
        else
            createInterface(*static_cast<interface::Interface*>(interface), key);
    }
}

void InterfaceManager::deactivateAll()
{
    while (!ospfInterfaceList.empty())
        ospfInterfaceList.erase(ospfInterfaceList.begin());
}

void InterfaceManager::syncNeighbors()
{
    for (auto& [_, iface] : ospfInterfaceList)
    {
        iface.ntable.syncUnicast();
    }
}

void InterfaceManager::resetNeighbors()
{
    for (auto& [_, iface] : ospfInterfaceList)
    {
        iface.ntable.resetNeighbors();
    }
}
} // namespace routing
