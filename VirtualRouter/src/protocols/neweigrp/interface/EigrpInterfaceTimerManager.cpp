// EigrpInterfaceTimerManager.cpp

#include <EigrpInterfaceTimerManager.h>

namespace Protocol
{
void EigrpInterface::startHello()
{
    // Mark hello as active

    if (!helloTimerActive.load(std::memory_order_relaxed) && helloTimerId.load(std::memory_order_relaxed) != 0)
    {
        return; // Timer already active
    }
    
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);
        if (helloStartTime.time_since_epoch().count() == 0)
        {
            helloStartTime = std::chrono::steady_clock::now();
        }

        auto nextExpiration = helloStartTime + std::chrono::seconds(configs->helloTime);

        uint32_t helloId = eigrpProcess.routingInstance->global.timeManager.addTimer(nextExpiration, [&, vrf = eigrpProcess.routingInstance->instanceName, as = eigrpProcess.asNumber, af = eigrpProcess.addressFamily]()
        {
            if (VirtualRouter* virtualRouter = eigrpProcess.routingInstance->global.getRoutingInstance(vrf))
            {
                if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
                {
                    if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
                    {
                        return;
                    }
                }
                else return;
            }
            else return;

            if (!helloTimerActive.load(std::memory_order_relaxed) || destroy.load(std::memory_order_relaxed)) return;
            helloDone.store(false, std::memory_order_release);
            try
            {
                {
                    std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
                    {
                        std::shared_lock<std::shared_mutex> lock(neighborMutex);
                        for (const auto& [_, neighbor] : neighbors)
                        {
                            if (neighbor->unicast)
                            {
                                unicastNeighbors.push_back(neighbor);
                            }
                        }
                    }

                    for (const auto& neighbor : unicastNeighbors)
                    {
                        sendHelloPacket(neighbor, true);
                    }
                    if (configs->multicastEnabled.load(std::memory_order_relaxed))
                    {
                        sendHelloPacket();
                    }
                }
            }
            catch (const std::exception &e)
            {
                Logger::getInstance().error() << "[StartHello] Exception in SendHelloPacket: " << e.what() << std::endl;
            }
            catch (...)
            {
                Logger::getInstance().error() << "[StartHello] Unknown exception in SendHelloPacket." << std::endl;
                // Handle unknown exceptions
            }

            // Reset and reschedule Hello timer
            {
                std::lock_guard<std::mutex> lock(helloTimerMutex);
                helloTimerId = 0; // Clear timer ID after packet is sent
                helloStartTime = std::chrono::steady_clock::now();
            }

            // Mark hello as done
            helloDone.store(true, std::memory_order_release);
            if (!destroy.load(std::memory_order_relaxed))
            {
                startHello(); // Reschedule
            }
        });
        helloTimerId.store(helloId, std::memory_order_release);
    }
}

void EigrpInterface::startHelloHelper()
{
    if (helloTimerActive.exchange(true))
    {
        // Hello timer is already active
        return;
    }
    helloTimerActive.store(true, std::memory_order_release);

    startHello();
}

void EigrpInterface::stopHello()
{
    helloTimerActive.store(false, std::memory_order_release);
    if (helloTimerId != 0)
    {
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(helloTimerId);
        if (!helloDone.load(std::memory_order_relaxed))
        {
            // Wait until the hello timer is fully shut down
            while (!helloDone.load(std::memory_order_relaxed))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        helloTimerId = 0;
    }
    {
        std::shared_lock<std::shared_mutex> neighborLock(neighborMutex);
        for (auto& [_, neighbor] : neighbors)
        {
            uint32_t holdTimerId = neighbor->holdTimerId.load(std::memory_order_relaxed);
            if (holdTimerId != 0)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(holdTimerId);
                neighbor->holdTimerId.store(0, std::memory_order_release);
            }
        }
    }
}

void EigrpInterface::startHoldTimer(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint16_t holdTime)
{
    uint32_t holdTimerId = neighbor->holdTimerId.load(std::memory_order_relaxed);
    if (holdTimerId != 0)
    {
        eigrpProcess.routingInstance->global.timeManager.cancelTimer(holdTimerId);
    }

    if (destroy.load(std::memory_order_relaxed)) return;
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
    holdTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, neighbor, neighborIp]()
                                                               { handleHoldTimeExpire(neighbor, neighborIp); });
    neighbor->holdTimerId.store(holdTimerId, std::memory_order_release);
}

void EigrpInterface::handleHoldTimeExpire(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
{
    if (neighbor)
    {
        if (eigrpProcess.configs.nonStopForwarding.load(std::memory_order_relaxed))
        {
            gracefulRestart(neighbor, neighborIp);
        }
        else
        {
            handleNeighborDown(neighbor, neighborIp);
        }
    }
}

void EigrpInterface::startActiveTimer(RoutingTable::Eigrp* route)
{
    IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.activeTime);
    // TODO Add SRTT into timeout

    // Schedule Active timer
    uint32_t activeTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, route, vrf = eigrpProcess.routingInstance->instanceName, as = eigrpProcess.asNumber, af = eigrpProcess.addressFamily]() {
        if (VirtualRouter* virtualRouter = eigrpProcess.routingInstance->global.getRoutingInstance(vrf))
        {
            if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
            {
                if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
                {
                    return;
                }
            }
            else return;
        }
        else return;
        handleActiveTimeExpire(route);
    });

    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);
        activeTimers[key] = activeTimerId;
    }
}

void EigrpInterface::cancelActiveTimer(const IPAddress& destination, uint8_t mask)
{
    IPPrefix key(destination.raw, mask, eigrpProcess.addressFamily);

    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);
        auto it = activeTimers.find(key);
        if (it != activeTimers.end())
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(it->second);
            activeTimers.erase(it);
        }
    }
}

void EigrpInterface::handleActiveTimeExpire(RoutingTable::Eigrp* route)
{
    IPPrefix queryKey(route->network.raw, route->mask, eigrpProcess.addressFamily);

    auto it = eigrpProcess.outstandingReplies.find(queryKey);
    if (it == eigrpProcess.outstandingReplies.end()) return;

    EigrpConfigs::ActiveRoute& queryInfo = it->second;

    std::vector<IPAddress> failedNeighbors;
    for (const auto& [neighborIp, outgoing] : queryInfo.pendingQueries)
        failedNeighbors.push_back(neighborIp);

    for (const auto& neighborIp : failedNeighbors)
    {
        std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
        for (const auto& [_, interface] : eigrpProcess.eigrpInterfaceList)
        {
            if (interface->neighbors.count(neighborIp))
            {
                handleNeighborDown(interface->neighbors[neighborIp], neighborIp);
            }
        }
    }

    // Clean up
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);

        // Cancel all SIA timers for each neighbor
        for (auto& [neighborIp, outgoing] : queryInfo.pendingQueries)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.siaTimerId);
        }
        activeTimers.erase(queryKey);
    }

    eigrpProcess.outstandingReplies.erase(queryKey);
}

uint32_t EigrpInterface::startSIATimer(RoutingTable::Eigrp* route, const IPAddress& neighborIp, EigrpConfigs::OutgoingQuery& outgoing)
{
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.stuckInActiveTime);

    // Schedule SIA-Query timer
    uint32_t timerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, route, neighborIp, vrf = eigrpProcess.routingInstance->instanceName, as = eigrpProcess.asNumber, af = eigrpProcess.addressFamily]()
    {
        if (VirtualRouter* virtualRouter = eigrpProcess.routingInstance->global.getRoutingInstance(vrf))
        {
            if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
            {
                if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
                {
                    return;
                }
            }
            else return;
        }
        else return;
        handleSIATimeout(route, neighborIp);
    });

    outgoing.siaTimerId = timerId;
    outgoing.lastSIARefreshTime = std::chrono::steady_clock::now();

    return timerId;
}

void EigrpInterface::handleSIATimeout(RoutingTable::Eigrp* route, const IPAddress& neighborIp)
{
    IPPrefix queryKey(route->network.raw, route->mask, eigrpProcess.addressFamily);

    auto it = eigrpProcess.outstandingReplies.find(queryKey);
    if (it == eigrpProcess.outstandingReplies.end()) return;

    if (it->second.originNeighbor == neighborIp) return;

    EigrpConfigs::ActiveRoute& queryInfo = it->second;

    auto neighborIt = queryInfo.pendingQueries.find(neighborIp);
    if (neighborIt == queryInfo.pendingQueries.end()) return;

    std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
    auto neighborEntry = neighbors.find(neighborIp);
    if (neighborEntry == neighbors.end()) return;

    EigrpConfigs::NeighborInfo* neighbor = neighborEntry->second;
    sendSIAQueryToNeighbor(neighbor, neighborIp);
    startSIATimer(route, neighborIp, neighborIt->second);
}

uint32_t EigrpInterface::startRetransmissionTimer(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const uint32_t sequenceNumber, double timeout)
{
    // Validate neighbor and timeout
    if (timeout <= 0.0 || !neighbor)
    {
        Logger::getInstance().error() << "Invalid timeout value or neighbor for retransmission timer." << std::endl;
        return 0;
    }
    
    {
        // Cancel any existing retransmission timers for this sequence number
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            // Cancel existing timer if any
            if (pktIt->second.timerId != 0)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(pktIt->second.timerId);
            }
        }
    }

    // Schedule a retransmission timer
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
    uint32_t timerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, neighbor, neighborIp, sequenceNumber]()
    {
        handleRetransmissionTimeout(neighbor, neighborIp, sequenceNumber);
    });

    // Update the timer ID in ReliablePacketInfo
    {
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        neighbor->reliablePackets[sequenceNumber].timerId = timerId;
    }

    return timerId;
}

void EigrpInterface::handleRetransmissionTimeout(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const uint32_t sequenceNumber)
{
    // Validate neighbor
    if (!neighbor || destroy.load(std::memory_order_relaxed)) return;


    // Retrieve the packet information under a shared lock
    EigrpConfigs::NeighborInfo::ReliablePacketInfo pktInfoCopy;
    {
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt == neighbor->reliablePackets.end()) return;
        pktInfoCopy = pktIt->second; // Copy the data for safe access
    }

    // Handle retransmission limit
    if (pktInfoCopy.retransmissionCount >= MAX_RETRANSMISSIONS)
    {
        if (pktInfoCopy.timerId != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(pktInfoCopy.timerId);
        }
        handleNeighborDown(neighbor, neighborIp);
        return;
    }

    // Resend the packet
    PacketBuilder retransmissionPacket(currentInterface);
    {
        // Construct and send the retransmission packet
        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::reserveIpv4(currentInterface, retransmissionPacket)
            : IPPacket::reserveIpv6(currentInterface, retransmissionPacket);
        retransmissionPacket.reserveHeader(HeaderType::EIGRP, 0);
        BuildEntry* nextHeader = retransmissionPacket.nextBuildHeader();
        if (!nextHeader) return;
        std::memcpy(nextHeader->buffer, pktInfoCopy.packet.headerBuffer, pktInfoCopy.packet.headerSize);
        retransmissionPacket.addTLVSize(pktInfoCopy.packet.headerSize);

        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = retransmissionPacket,
                .destIp = pktInfoCopy.packet.destination.raw,
                .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                .protocolType = Variable::IP::eigrp
            };

            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::buildIpv4(build)
                : IPPacket::buildIpv6(build);
        }
    }

    // Increment retransmission timer safely
    uint32_t newTimerId;
    {
        std::lock_guard<std::mutex> dataLock(neighbor->reliableMutex);
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            pktIt->second.retransmissionCount += 1;
            pktIt->second.sendTime = std::chrono::steady_clock::now(); // Update the send time.
        }
    }

    // Restart the retransmission timer
    {
        std::unique_lock<std::mutex> dataLock(neighbor->reliableMutex);
        neighbor->rto = std::min(neighbor->rto * 2.0, 60.0);
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            dataLock.unlock();
            newTimerId = startRetransmissionTimer(neighbor, neighborIp, sequenceNumber, neighbor->rto);
            dataLock.lock();
            pktIt->second.timerId = newTimerId;
        }
    }
}

void EigrpInterface::setupReliablePacket(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet& packet, uint32_t sequenceNum)
{
    {
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        if (neighbor->reliablePackets.find(sequenceNum) != neighbor->reliablePackets.end())
        {
            return;
        }
    }

    // Create ReliablePacketInfo
    EigrpConfigs::NeighborInfo::ReliablePacketInfo pktInfo(packet);
    pktInfo.sendTime = std::chrono::steady_clock::now();
    pktInfo.retransmissionCount = 0;
    pktInfo.timerId = startRetransmissionTimer(neighbor, neighborIp, sequenceNum, neighbor->rto);

    // Store the packet
    {
        std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
        neighbor->reliablePackets[sequenceNum] = pktInfo;
    }
}
}
