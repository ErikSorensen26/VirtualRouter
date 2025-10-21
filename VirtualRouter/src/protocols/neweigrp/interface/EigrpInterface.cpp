 / EigrpInterface.cpp

#include "EigrpInterface.h"

namespace Protocol
{
EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, EigrpConfigs::InterfaceConfigs* intConfigs, Interface* interface)
    : eigrpProcess(eigrpSystem),
      configs(intConfigs),
      currentInterface(interface),
      currentInterfaceInfo(&interface->configs)
{
    {
        // Start router

        // Store initial IP of interface
        interfaceKey = currentInterfaceInfo->key;

        // Add pending summary routes if needed
        for (const auto& [network, mask] : configs->pendingSummaryRoutes)
        {
            addSummaryRoute(network.raw, mask);
        }
        configs->pendingSummaryRoutes.clear();

        // Gather locked values for local metric calculation
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
        uint32_t bandwidth = currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
        uint8_t load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
        uint32_t key = currentInterfaceInfo->key;
        uint8_t reliability = 255;

        // Check if this interface is passive
        bool passive = false;
        {
            std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
            if (eigrpProcess.configs.passiveInterfaces.find(key) != eigrpProcess.configs.passiveInterfaces.end())
            {
                passive = true;
            }
        }
        if (passive)
        {
            setPassive(true);
        }

        // TODO add dampening for recalculation

        // Recalculate metrics if new lowest bandwidth is found
        if (eigrpProcess.configs.lowestBandwidth.load(std::memory_order_relaxed) > bandwidth)
        {
            eigrpProcess.configs.lowestBandwidth.store(bandwidth, std::memory_order_release);
            // Recalculate local metrics on all interfaces
            {
                //std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
                for (const auto& [id, interfacePtr] : eigrpProcess.eigrpInterfaceList)
                {
                    if (interfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;

                    uint32_t intDelay = interfacePtr->currentInterfaceInfo->delay.load(std::memory_order_relaxed);
                    uint8_t intLoad = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
                    interfacePtr->configs->localMetric = interfacePtr->eigrpProcess.calculateLocalLinkCost(intLoad, intDelay, reliability);
                };
            }
        }
        else
        {
            std::shared_lock<std::shared_mutex> metricLock(configs->configsMutex);
            configs->localMetric = eigrpProcess.calculateLocalLinkCost(load, delay, reliability);
        }

        // Handle unciast neighbors
        std::vector<IPAddress> unicastNeighbors;
        {
            std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
            if (!eigrpProcess.configs.unicastNeighbors[key].empty())
            {
                // Disable multicast if unicast neighbors are present
                configs->multicastEnabled.store(false, std::memory_order_release);
                
                // Store neighbors to add
                for (auto neighbor : eigrpProcess.configs.unicastNeighbors[key])
                {
                    unicastNeighbors.emplace_back(neighbor);
                }
            }
        }
        //Add unicast neighbors if any are present
        {
            for (auto neighbor : unicastNeighbors)
            {
                addUnicastNeighbor(neighbor);
            }
        }

        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            // Start hello for interface if interface is not passive
            if (!passive)
            {
                helloStartTime = std::chrono::steady_clock::now();
                startHelloHelper();
                sendHelloPacket();
            }
        }
    }
}

EigrpInterface::~EigrpInterface()
{
    destroy.store(true, std::memory_order_seq_cst);
    stopHello();

    // Wait for any in-progress hello packets
    while (!helloDone.load(std::memory_order_seq_cst))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    AddressFamily addressFamily = eigrpProcess.addressFamily;
    uint8_t mask = 32;
    uint8_t network[16];
    {
        if (addressFamily == AddressFamily::IPv4)
        {
            mask = currentInterface->configs.ipv4.getMask();
            uint8_t ipAddress[4];
            Functions::computeNetworkAddress(network, currentInterface->configs.ipv4.getAddress(ipAddress), mask, AddressFamily::IPv4);
        }
        else if (addressFamily == AddressFamily::IPv6)
        {
            IPAddress ip;
            mask = currentInterface->configs.ipv6.getGlobalUnicastPair(ip.raw);
            if (ip.v6 != 0)
            {
                Functions::computeNetworkAddress(network, ip.raw, mask, AddressFamily::IPv6);
            }
        }
    }

    auto globalRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(network, mask, addressFamily, eigrpProcess.asNumber);
    if (globalRoute)
    {
        if (globalRoute && globalRoute->routeType == RoutingTable::Eigrp::RouteType::CONNECTED)
        {
            // Remove if valid and is a connected route
            eigrpProcess.routingInstance->routingTable.removeEigrp(network, mask, addressFamily, eigrpProcess.asNumber);
        }
    }

    // Remove interface from routing table if able to
    sendHelloPacket(nullptr); // Termination message

    auto interface = currentInterface;
    currentInterface = nullptr;

    // Remove all active timers
    {
        std::lock_guard<std::mutex> activeLock(activeTimerMutex);
        for (auto& [_, id] : activeTimers)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(id);
        }
        for (auto& [_, query] : eigrpProcess.outstandingReplies)
        {
            for (auto& [_, id] : query.pendingQueries)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(id.siaTimerId);
            }
        }
    }

    // Remove query timers
    for (const auto& [key, query] : eigrpProcess.outstandingReplies)
    {
        for (const auto& query : query.pendingQueries)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(query.second.siaTimerId);
        }
    }
    for (const auto& [key, activeId] : activeTimers)
    {
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(activeId);
    }
    
    // Aquire lock to modify neighbors
    std::unordered_map<IPAddress, EigrpConfigs::NeighborInfo*> neighborsCopy;
    {
        std::unique_lock<std::shared_mutex> lock(neighborMutex);
        neighborsCopy = neighbors;
    }

    for (auto& [ip, neighbor] : neighborsCopy)
    {
        handleNeighborDown(neighbor, ip);
    }

    {
        std::shared_lock<std::shared_mutex> lock(configs->configsMutex);
        for (const auto& sr : configs->summaryRoutes)
        {
            if (!sr.isAuto)
                configs->pendingSummaryRoutes.emplace_back(sr.summary->network, sr.summary->mask);
        }
        configs->summaryRoutes.clear();
    }

    // Remove interface from other tables
    uint32_t id = eigrpProcess.asNumber;
    if (interface->eigrpInterfaceList.find(id) != interface->eigrpInterfaceList.end())
    {
        if (addressFamily == AddressFamily::IPv4)
        {
            interface->eigrpInterfaceList[id]->IPv4 = nullptr;
        }
        else if (addressFamily == AddressFamily::IPv6)
        {
            interface->eigrpInterfaceList[id]->IPv6 = nullptr;
        }
        if (!interface->eigrpInterfaceList[id]->IPv4 && !interface->eigrpInterfaceList[id]->IPv6)
        {
            interface->eigrpInterfaceList.erase(id);
        }
    }

    // Remove all routes/summary routes
    eigrpProcess.routingInstance->routingTable.removeEigrpWithOutInterface(eigrpProcess.addressFamily, eigrpProcess.asNumber, interfaceKey);

    //Logger::getInstance().info() << "EigrpInterface destroyed and all timers canceled." << std::endl;
}

void EigrpInterface::setPassiveMode(bool passive)
{
    configs->isPassive.store(passive, std::memory_order_release);
    if (passive)
    {
        stopHello();
    }
    else
    {
        helloStartTime = std::chrono::steady_clock::now();
        startHelloHelper();
        sendHelloPacket();
    }

    Logger::getInstance().info() << "Interface set to " << (passive ? "passive" : "active") << " modeo." << std::endl;
}

void EigrpInterface::setMulticast(bool state)
{
    if (state && !config->multicastEnabled.load(std::memory_order_relaxed))
    {
        configs->multicastEnabled.store(true, std::memory_order_release);
    {
    else if (config->multicast.load(std::memory_order_relaxed))
    {
        configs->multicastEnabled.store(false, std::memory_order_release);
        for (auto it = neighbors.begin(); it != neighbors.end();)
        {
            if (!it->second->unicast)
            {
                handleNeighborDown(it->second, it->first);
            }
            else
            {
                it++;
            }
        }
    }
}

const uint8_t* EigrpInterface::multicastEnabled()
{
    return (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Multicast::Eigrp::address : Variable::Multicast::Eigrp::addressv6;
}

inline IPAddress EigrpInterface::localAddress()
{
    IPAddress ip;
    ip.isV6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
    ip.isV6 ? currentInterfaceInfo->ipv4.getAddress(ip.raw) : currentInterfaceInfo->ipv6.getLocalAddress(ip.raw);
    return ip;
}
}
