#include <Ndp.h>
#include <Interface.h>
#include <IPPacket.h>

namespace Protocol
{
    // Constructor: Initiates the NDP object with the given interface
    Ndp::Ndp(Interface& CurrentInterface)
        : currentInterface(&CurrentInterface), running(true)
    {

    }

    // Destructor
    Ndp::~Ndp()
    {
        shutdown();
    }

    void Ndp::shutdown()
    {
        running.store(false, std::memory_order_release);
        // STOP TIMERS

        // Clear resources
        {
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            neighborReplyStatus.clear();
        }
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp.clear();
        }
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.clear();
        }

        // Join any remaining threads
        for (auto& t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        threads.clear();
    }

    void Ndp::addNdpEntry(const ByteString& targetIp, const ByteString& targetMac)
    {
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        if (ndpCache.size() >= configs.interfaceLimit.load(std::memory_order_release))
        {
            // Remove an entry to enforce a limit.
            ndpCache.erase(ndpCache.begin());
        }
        NdpCacheEntry entry;
        entry.macAddress = targetMac;
        entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.cacheExpire.load(std::memory_order_relaxed));
        entry.state = NudState::REACHABLE;
        // Schedule a timer for NUD reachable time.
        entry.timerId = TimeManager::getInstance().addTimer(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
            [this, targetIp]() { onNudTimeout(targetIp); });
        ndpCache[targetIp] = entry;
    }

    // Get MAC address for the given ip
    ByteString* Ndp::getMac(const ByteString& ip)
    {
        std::shared_lock<std::shared_mutex> lock(ndpCacheMutex);
        auto it = ndpCache.find(ip);
        if (it != ndpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime)
        {
            return &it->second.macAddress;
        }
        return nullptr;
    }

    void Ndp::resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend)
    {
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp[targetIp].push(std::move(packetToSend));
        }
        sendNeighborSolicitation(targetIp);
    }

    void Ndp::sendNeighborSolicitation(const ByteString& targetIp)
    {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            if (pendingRequests.count(targetIp)) return;
            pendingRequests.insert(targetIp);
        }

        {
            // Create reply status entry if not already present
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            if (neighborReplyStatus.find(targetIp) == neighborReplyStatus.end())
            {
                neighborReplyStatus[targetIp] = std::make_shared<std::atomic<bool>>(false);
            }
        }

        threads.emplace_back(&Ndp::handleNeighborSolicitation, this, targetIp);
    }

    void Ndp::handleNeighborSolicitation(const ByteString& targetIp)
    {
        int retries = 0;
        const auto nsInterval = std::chrono::milliseconds(configs.nsInterval);
        const auto maxRetries = configs.nudRetries.load(std::memory_order_release);

        while (running.load(std::memory_order_relaxed) && retries < maxRetries)
        {
            {
                std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
                if (neighborReplyStatus[targetIp]->load(std::memory_order_relaxed))
                {
                    // Received a reply; no need to continue probing.
                    break;
                }
            }

            // Create the packet
            PacketInfo nsPacket;
            auto iface = currentInterface->Get();
            if (iface)
            {
                nsPacket = neighborSolicitation(targetIp, &iface->macAddress);
            }

            // Set the IP header and send the packet
            IPPacket::buildIp(currentInterface, nsPacket, generateMulticastSolicitationAddress(targetIp), nullptr, nullptr, 0, 255, Variable::IP::icmpv6);

            // Wait for a reply (using the conditional variable wait).
            if (waitForNeighborReply(targetIp, nsInterval))
            {
                break;
            }
            retries++;
        }
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(targetIp);
        }
    }

    void Ndp::handleRouteSolicitation(const ByteString& targetIp)
    {

    }

    bool Ndp::waitForNeighborReply(const ByteString& targetIp, const std::chrono::milliseconds& timeout)
    {
        std::unique_lock<std::mutex> lock(neighborReplyStatusMutex);
        auto end = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < end)
        {
            if (threadCV.wait_until(lock, end, [this, &targetIp]() {
                    return !running.load() || (neighborReplyStatus.count(targetIp) && neighborReplyStatus[targetIp]->load(std::memory_order_relaxed));
                }))
            {
                return neighborReplyStatus[targetIp]->load(std::memory_order_relaxed);
            }
        }
        return false;
    }

    void Ndp::receiveNeighborAdvertisement(const IcmpV6Header& receivedNA)
    {
        ByteString targetIp = receivedNA.payload;
        ByteString mac;

        for (const auto& opt : receivedNA.options)
        {
            // Extract MAC from options
            if (opt.option == Variable::ICMPv6::Option::target && opt.value.size() == 6)
            {
                mac = opt.value;
                break;
            }
            else
            {
                return;
            }
        }

        {
            NdpCacheEntry entry;
            entry.macAddress = mac;
            entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.cacheExpire.load(std::memory_order_relaxed));
            entry.state = NudState::REACHABLE;

            std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
            if (entry.timerId)
                TimeManager::getInstance().cancelTimer(entry.timerId);
            entry.timerId = TimeManager::getInstance().addTimer(
                std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                [this, targetIp]() { onNudTimeout(targetIp); });
            ndpCache[targetIp] = entry;
        }
        {
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            if (neighborReplyStatus.count(targetIp))
            {
                neighborReplyStatus[targetIp]->store(true, std::memory_order_release);
                threadCV.notify_all();
            }
        }
        processQueuedPackets(targetIp, mac);
    }

    void Ndp::processQueuedPackets(const ByteString& targetIp, const ByteString& macAddress)
    {
        std::queue<PacketInfo> packets;
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            if (packetQueuePerIp.count(targetIp))
            {
                packets = std::move(packetQueuePerIp[targetIp]);
                packetQueuePerIp.erase(targetIp);
            }
        }

        while (!packets.empty())
        {
            PacketInfo pkt = packets.front();
            packets.pop();
            currentInterface->enqueuePacket(pkt, macAddress);
        }

        threadCV.notify_all();
    }

    void Ndp::startNudTimer(const ByteString& targetIp)
    {
        // Schedule a timer that will trigger NUD after the reachableTime expires.
        auto expiration = std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed));
        uint32_t timerId = TimeManager::getInstance().addTimer(expiration, [this, targetIp]() {
            onNudTimeout(targetIp);
        });
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        if (ndpCache.find(targetIp) != ndpCache.end())
        {
            ndpCache[targetIp].timerId = timerId;
        }
    }

    void Ndp::onNudTimeout(const ByteString& targetIp)
    {
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        auto it = ndpCache.find(targetIp);
        if (it == ndpCache.end())
            return;

        // Transition between NUD states:
        if (it->second.state == NudState::REACHABLE)
        {
            // No recent traffic - mark as stale
            it->second.state =  NudState::STALE;
        }
        else if (it->second.state == NudState::STALE)
        {
            // When traffic triggers usage, transition to PROBE and send NS.
            it->second.state = NudState::PROBE;
            lock.unlock();
            sendNeighborSolicitation(targetIp);
        }
        else if (it->second.state == NudState::PROBE)
        {
            // After probing without response, remove the cache entry
            ndpCache.erase(it);
        }
    }
    
    ByteString Ndp::generateMulticastSolicitationAddress(const ByteString& targetIp)
    {
        if (targetIp.size() != 16)
        {
            return "";
        }
        return Variable::Multicast::ICMPv6::solicitationAddress + targetIp.substr(13).toString();
    }

    PacketInfo Ndp::neighborSolicitation(const ByteString& targetIp, ByteString const* currentMac)
    {
        PacketInfo packet;
        IcmpV6Header icmp;

        icmp.type = Variable::ICMPv6::Type::ndpNeighborSolicitation;
        icmp.code = ByteString("\x00", 1);
        icmp.reserved = ByteString("\x00\x00\x00\x00", 4);
        icmp.payload = targetIp;

        if (currentMac)
        {
            IcmpV6Header::Option target;
            target.option = Variable::ICMPv6::Option::target;
            target.length = ByteString("\x01", 1); // 8 Bytes
            target.value = *currentMac;
            
            icmp.options.push_back(std::move(target));
        }

        packet.Layer3.push_back(std::move(icmp));

        return packet;
    }

    PacketInfo Ndp::neighborAdvertisement(const ByteString& currentMac, ByteString const* targetIp)
    {
        PacketInfo packet;
        IcmpV6Header icmp;

        icmp.type = Variable::ICMPv6::Type::ndpNeighborAdvertisement;
        icmp.code = ByteString("\x00", 1);
        if (targetIp)
        {
            icmp.reserved = ByteString("\x60\x00\x00\x00", 4); // Router and Override set
        }
        else
        {
            icmp.reserved = ByteString("\xa0\x00\x00\x00", 4); // Router and Override set
        }
            
        {
            std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
            icmp.payload = currentInterface->configs.ipv6.ipAddress;
        }

        IcmpV6Header::Option target;
        target.option = Variable::ICMPv6::Option::target;
        target.length = ByteString("\x01", 1); // 8 Bytes
        target.value = currentMac;
        
        icmp.options.push_back(std::move(target));

        packet.Layer3.push_back(std::move(icmp));

        return packet;
    }

    PacketInfo Ndp::routeSolicitation(ByteString& currentMac)
    {
        PacketInfo packet;
        IcmpV6Header icmp;

        icmp.type = Variable::ICMPv6::Type::ndpRouteSolicitation;
        icmp.code = ByteString("\x00", 1);
        icmp.reserved = ByteString("\x00\x00\x00\x00", 4);
        
        IcmpV6Header::Option source;
        source.option = Variable::ICMPv6::Option::source;
        source.length = ByteString("\x01", 1);
        source.value = currentMac;

        icmp.options.push_back(std::move(source));

        packet.Layer3.push_back(std::move(icmp));

        return packet;
    }

    PacketInfo Ndp::routeAdvertisment(const ByteString& currentMac, uint8_t ttl, uint16_t lifetime, uint16_t reachableTime, uint16_t retransTimer)
    {
        PacketInfo packet;
        IcmpV6Header icmp;

        icmp.type = Variable::ICMPv6::Type::ndpRouteAdvertisement;
        icmp.code = ByteString("\x00", 1);
        icmp.reserved = Functions::numToByte(ttl, 1) +
            ByteString("\x00", 1) + // Flags
            Functions::numToByte(lifetime, 2);
        icmp.payload = Functions::numToByte(reachableTime, 4) +
            Functions::numToByte(retransTimer, 4);

        IcmpV6Header::Option source;
        source.option = Variable::ICMPv6::Option::source;
        source.length = ByteString("\x01", 1);
        source.value = currentMac;
        icmp.options.push_back(std::move(source));

        IcmpV6Header::Option mtu;
        source.option = Variable::ICMPv6::Option::mtu;
        source.length = ByteString("\x01", 1);
        source.value = ByteString("\x00\x00", 2) + // Reserved
            Functions::numToByte(currentInterface->Get()->mtu);
        icmp.options.push_back(std::move(source));

        packet.Layer3.push_back(std::move(icmp));
        
        return packet;
    }

    void Ndp::sendNeighborAdvertisement(const ByteString& destMac, ByteString const* targetIp)
    {
        auto iface = currentInterface->Get();
        if (iface)
        {
            ByteString ip;
            {
                std::shared_lock<std::shared_mutex> lock(iface->ipMutex);
                ip = iface->ipv6.ipAddress;
            }
            PacketInfo naPacket = neighborAdvertisement(iface->macAddress, targetIp);
            
            // Set the IP header and send the packet.
            IPPacket::buildIp(currentInterface, naPacket, targetIp ? *targetIp : Variable::IPv6::multicast, nullptr, &destMac, 0, 255, Variable::IP::icmpv6);
        }
    }
    
    void Ndp::sendRouteSolicitation(const ByteString& targetIp)
    {
        auto iface = currentInterface->Get();
        if (iface)
        {
            PacketInfo rsPacket = routeSolicitation(iface->macAddress);

            // Set the IP header and send the packet.
            IPPacket::buildIp(currentInterface, rsPacket, generateMulticastSolicitationAddress(targetIp), nullptr, nullptr, 0, 255, Variable::IP::icmpv6);
        }
    }

    void Ndp::sendRouteAdvertisement(const ByteString& targetMac, const ByteString& targetIp)
    {
        auto iface = currentInterface->Get();
        if (iface)
        {
            // Gather interface values.
            uint8_t ttl = iface->ttl;
            uint16_t lifetime = configs.lifetime.load(std::memory_order_relaxed);
            uint16_t reachableTime = configs.reachableTime.load();
            uint16_t retransTimer = 1000;
            PacketInfo raPacket = routeAdvertisment(iface->macAddress, ttl, lifetime, reachableTime, retransTimer);
            
            // Set the IP header and send the packet.
            IPPacket::buildIp(currentInterface, raPacket, targetIp, nullptr, &targetMac, 0, 255, Variable::IP::icmpv6);
        }
    }

    void Ndp::receiveRouteAdvertisement(const IcmpV6Header& receivedRA)
    {
        if (configs.suppressRA.load(std::memory_order_relaxed))
            return;

        // Add to routing table.
    }

    void Ndp::autoConfig()
    {
        if (configs.autoConfigDefaultRoute.load(std::memory_order_relaxed))
        {

        }
        if (configs.autoConfigPrefix.load(std::memory_order_relaxed))
        {

        }
    }

    void Ndp::duplicateAddressDetection(bool localLink)
    {
        auto iface = currentInterface->Get();
        if (iface)
        {
            // Retries tantative addresses from the interface.
            std::vector<ByteString> tentativeAddresses = currentInterface->getTentativeAddress();
            for (const auto& addr : tentativeAddresses)
            {
                int attempts = 0;
                bool duplicate = false;
                while (attempts < configs.dadAttempts.load(std::memory_order_relaxed))
                {
                    PacketInfo dadNS;
                    dadNS = neighborSolicitation(addr, &iface->macAddress);
                    IPPacket::buildIp(currentInterface, dadNS, generateMulticastSolicitationAddress(addr), nullptr, nullptr, 0, 255, Variable::IP::icmpv6);
                    if (waitForNeighborReply(addr, std::chrono::milliseconds(configs.dadTime.load(std::memory_order_relaxed))))
                    {
                        duplicate = true;
                        break;
                    }
                    attempts++;
                }
                if (duplicate)
                {
                    currentInterface->markAddressDuplicate(addr, localLink);
                }
                else
                {
                    std::shared_lock<std::shared_mutex> lock(iface->ipMutex);
                    iface->ipv6.validateAddress(localLink);
                }
            }
        }
    }

}
