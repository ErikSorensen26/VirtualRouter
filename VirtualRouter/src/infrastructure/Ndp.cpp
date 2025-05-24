// TODO naglean
// TODO nudigp
#include <Ndp.h>
#include <IPPacket.h>
#include <TimeManager.h>
#include <VirtualRouter.h>
#include <Global.h>

namespace Protocol
{
    // Constructor: Initiates the NDP object with the given interface
    Ndp::Ndp(Interface& iface)
        : currentInterface(&iface),
        global(iface.routingInstance->global)
    {
        // Initialize global configs
        configs.refresh = global.configs.ndp.refresh.load(std::memory_order_relaxed);
        configs.loggingRate = global.configs.ndp.loggingRate.load(std::memory_order_relaxed);
        configs.cacheExpire = global.configs.ndp.cacheExpire.load(std::memory_order_relaxed);
        configs.dadTime = global.configs.ndp.dadTime.load(std::memory_order_relaxed);
        configs.reachableTime = global.configs.ndp.reachableTime.load(std::memory_order_relaxed);
        configs.interfaceLimit = global.configs.ndp.interfaceLimit.load(std::memory_order_relaxed);

        initializeNdp();
    }

    void Ndp::initializeNdp()
    {
        // Add static neighbors
        std::shared_lock<std::shared_mutex> lock(global.configs.ndp.neighborMutex);
        for (const auto& [ip, neighbor] : global.configs.ndp.neighbors)
        {
            if (neighbor.interface.first == currentInterface->configs.interfaceType &&
                neighbor.interface.second == currentInterface->configs.id)
            {
                addNdpEntry(ip, neighbor.macAddress, false, true);
            }
        }
        running.store(true, std::memory_order_relaxed);
        if (!configs.raSuppressAll.load(std::memory_order_relaxed))
            scheduleNextRA();
    }

    // Destructor
    Ndp::~Ndp()
    {
        shutdown();
    }

    void Ndp::shutdown()
    {
        running.store(false, std::memory_order_release);

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
            nsRetryCount.clear();
            for (auto& [_, timerId] : nsRetryTimers)
                global.timeManager.cancelTimer(timerId);
            nsRetryTimers.clear();
            for (auto& [_, timerId] : dadTimers)
                global.timeManager.cancelTimer(timerId);
            dadTimers.clear();
            for (auto& [_, timerId] : pendingDadReschedules)
                global.timeManager.cancelTimer(timerId);
            pendingDadReschedules.clear();
        }
        {
            std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
            for (auto& [ip, entry] : ndpCache)
            {
                global.timeManager.cancelTimer(entry.timerId);
            }
            ndpCache.clear();
            insertionOrder.clear();
        }
        {
            for (const auto& timerId : raTimerIds)
            {
                global.timeManager.cancelTimer(timerId);
            }
            raTimerIds.clear();
        }
    }

    void Ndp::addNdpEntry(const ByteString& targetIp, const ByteString& targetMac, bool proxy, bool isStatic)
    {
        if (!isStatic)
        {
            std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
            uint32_t limit = configs.interfaceLimit.load(std::memory_order_relaxed);
            if (limit != 0 && ndpCache.size() >= limit)
            {
                // Remove an entry to enforce a limit.
                ByteString evicted = insertionOrder.front();
                insertionOrder.pop_front();
                global.timeManager.cancelTimer(ndpCache[evicted].timerId);
                ndpCache.erase(evicted);
            }
        }
        NdpCacheEntry entry;
        entry.macAddress = targetMac;
        entry.state = NudState::REACHABLE;
        if (!isStatic)
        {
            entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.cacheExpire.load(std::memory_order_relaxed));
        }

        // Only start refresh timer if enabled and dynamic
        if (!isStatic)
        {
            uint8_t refresh = global.configs.ndp.nudRefreshPeriod.load(std::memory_order_relaxed);
            if (refresh > 0)
            {
                entry.timerId = global.timeManager.addTimer(
                    std::chrono::steady_clock::now() + std::chrono::seconds(refresh),
                    [this, targetIp]() {
                        refreshNeighborEntry(targetIp);
                    }
                );
            }
            else
            {
                // Schedule a timer for NUD reachable time.
                entry.timerId = global.timeManager.addTimer(
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                    [this, targetIp]() { onReachableTimeout(targetIp);
                });
            }
        }


        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        if (isStatic)
        {
            staticNdpCache[targetIp] = entry;
        }
        else
        {
            // Add dynamic entry
            ndpCache[targetIp] = entry;
            insertionOrder.push_back(targetIp);

            if (proxy)
            {
                proxyEntries[targetIp] = targetMac;
            }
        }
        processQueuedPackets(targetIp, targetMac);
    }

    // Get MAC address for the given ip
    ByteString* Ndp::getMac(const ByteString& ip)
    {
        std::shared_lock<std::shared_mutex> lock(ndpCacheMutex);
        {
            std::shared_lock<std::shared_mutex> neighborLock(global.configs.ndp.neighborMutex);
            auto staticIt = staticNdpCache.find(ip);
            if (staticIt != staticNdpCache.end())
            {
                return &staticIt->second.macAddress;
            }
        }
        auto it = ndpCache.find(ip);
        if (it != ndpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime)
        {
            return &it->second.macAddress;
        }
        return nullptr;
    }

    void Ndp::initiateSlaac()
    {
        if (!configs.slaacEnabled.load(std::memory_order_relaxed))
            return;

        // Send a Router Solicitation to get fresh RA with A-bit prefixes
        auto& iface = currentInterface->configs;
        PacketInfo rs = routeSolicitation(iface.getMac());

        IPPacket::buildIp(
            currentInterface,
            rs,
            Variable::Multicast::ICMPv6::allRouters,
            nullptr,
            nullptr,
            0, 255,
            Variable::IP::icmpv6
        );
    }

    void Ndp::resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend)
    {
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp[targetIp].push(std::move(packetToSend));
        }

        if (global.configs.nsfActive.load(std::memory_order_relaxed))
        {
            auto now = std::chrono::steady_clock::now();
            auto gracePeriod = std::chrono::seconds(global.configs.ndp.nsfConvergenceTime.load(std::memory_order_relaxed));
            if (now - global.configs.nsfStartTime < gracePeriod)
            {
                // Within NFS window - check resolution throttle
                uint32_t maxRes = global.configs.ndp.nsfThrottleResolutions.load(std::memory_order_relaxed);
                if (nfsResolutionCount.fetch_add(1, std::memory_order_seq_cst) >= maxRes)
                {
                    return; // Drop new resolution due to throttle limit
                }
            }
        }
        else if (nfsResolutionCount.load(std::memory_order_relaxed) != 0)
        {
            nfsResolutionCount.store(0, std::memory_order_release);
        }

        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        auto it = ndpCache.find(targetIp);
        if (it != ndpCache.end())
        {
            bool strict = global.configs.ndp.strictMode;

            if (it->second.state == NudState::STALE)
            {
                startNud(targetIp, it->second, lock);
            }
            else if (it->second.state == NudState::DELAY)
            {
                if (strict)
                {
                    // Delay isn't trusted in strict mode
                    startNud(targetIp, it->second, lock);
                }
                {
                    return;
                }
            }
            else if (it->second.state == NudState::REACHABLE)
            {
                if (strict)
                {
                    // Still probe it even if reachable, in strict mode
                    startNud(targetIp, it->second, lock);
                }
                else
                {
                    lock.unlock();
                    processQueuedPackets(targetIp, it->second.macAddress);
                }
            }
        }
        else
        {
            // Enforce resolution-limit (only for new unknown neighbors)
            uint32_t maxResolution = global.configs.ndp.resolutionLimit.load(std::memory_order_relaxed);
            if (maxResolution != 0 && currentResolvingNeighbors.load(std::memory_order_relaxed) >= maxResolution)
            {
                // Drop resolution entirely
                queuedResolution.insert(targetIp);
                return;
            }

            currentResolvingNeighbors.fetch_add(1, std::memory_order_seq_cst);

            lock.unlock();
            sendNeighborSolicitation(targetIp); // Initial Learning
        }
    }

    void Ndp::sendNeighborSolicitation(const ByteString& targetIp)
    {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            if (pendingRequests.count(targetIp)) return;
            pendingRequests.insert(targetIp);
            nsRetryCount[targetIp] = 0;
        }
        scheduleNeighborSolicitation(targetIp);
    }

    void Ndp::receiveNeighborAdvertisement(const IcmpV6Header& receivedNA, const ByteString& sourceIp)
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
        }
        if (mac.empty()) return;

        {
            {
                std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                for (const auto& addr : currentInterface->configs.ipv6.globalAddresses)
                {
                    if (addr->ip == targetIp && addr->tentative && sourceIp == Variable::IPv6::source)
                    {
                        std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
                        neighborReplyStatus[targetIp] = true;
                        return;
                    }
                }
            }

            bool newResolution = false;
            {
                std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
                auto it = ndpCache.find(targetIp);
                if (it != ndpCache.end())
                {
                    if (it->second.state == NudState::ACTIVE)
                    {
                        newResolution = true;
                    }
                    it->second.macAddress = mac;
                    it->second.state = NudState::REACHABLE;
                    it->second.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.cacheExpire.load(std::memory_order_relaxed));
                    global.timeManager.cancelTimer(it->second.timerId);
                    it->second.timerId = 0;
                    uint8_t refresh = global.configs.ndp.nudRefreshPeriod.load(std::memory_order_relaxed);
                    if (refresh > 0)
                    {
                        it->second.timerId = global.timeManager.addTimer(
                            std::chrono::steady_clock::now() + std::chrono::seconds(refresh),
                            [this, targetIp]() {
                                refreshNeighborEntry(targetIp);
                            }
                        );
                    }
                    else
                    {
                        it->second.timerId = global.timeManager.addTimer(
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                            [this, targetIp]() { onReachableTimeout(targetIp); }
                        );
                    }
                }
                else
                {
                    NdpCacheEntry entry;
                    entry.macAddress = mac;
                    entry.state = NudState::REACHABLE;
                    entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.cacheExpire.load(std::memory_order_relaxed));
                    uint8_t refresh = global.configs.ndp.nudRefreshPeriod.load(std::memory_order_relaxed);
                    if (refresh > 0)
                    {
                        entry.timerId = global.timeManager.addTimer(
                            std::chrono::steady_clock::now() + std::chrono::seconds(refresh),
                            [this, targetIp]() {
                                refreshNeighborEntry(targetIp);
                            }
                        );
                    }
                    else
                    {
                        entry.timerId = global.timeManager.addTimer(
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                            [this, targetIp]() { onReachableTimeout(targetIp); }
                        );
                    }
                    ndpCache[targetIp] = entry;
                    newResolution = true;
                }
            }
            if (newResolution && currentResolvingNeighbors.load(std::memory_order_relaxed) != 0)
            {
                currentResolvingNeighbors.fetch_sub(1, std::memory_order_seq_cst);
            }
            if (!queuedResolution.empty())
            {
                ByteString resolveIp = *queuedResolution.begin();
                sendNeighborSolicitation(resolveIp);
            }
        }
        {
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            if (neighborReplyStatus.count(targetIp))
            {
                neighborReplyStatus[targetIp] = true;
            }
        }
        processQueuedPackets(targetIp, mac);
    }

    void Ndp::receiveNeighborSolicitation(const IcmpV6Header& nsHeader, const ByteString& srcIp, const ByteString& srcMac)
    {
        const ByteString& targetIp = nsHeader.payload;

        ByteString replyMac;
        bool isOwned = false;
        bool isProxy = false;

        {
            if (currentInterface->configs.hasAddress(targetIp))
            {
                replyMac = currentInterface->configs.getMac();
                isOwned = true;
            }
            else if (proxyEntries.count(targetIp))
            {
                replyMac = proxyEntries[targetIp];
                isProxy = true;
            }
        }

        if (!isOwned && !isProxy) return;

        // Always respond to DAD (unspecified source IP = DAD probe)
        //the S flag will be 0 in this case (unsolicited NA)
        PacketInfo na = neighborAdvertisement(replyMac, isProxy ? &targetIp : &srcIp);

        if (srcMac.empty() || srcIp == Variable::IPv6::source)
        {
            // Multicast NA for DAD response or missing MAC
            IPPacket::buildIp(currentInterface, na, Variable::IPv6::multicast, &targetIp, nullptr, 0, 255, Variable::IP::icmpv6);
        }
        else
        {
            // Unicast NA back to sender
            IPPacket::buildIp(currentInterface, na, srcIp, &targetIp, &srcMac, 0, 255, Variable::IP::icmpv6);
        }
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
    }

    void Ndp::onReachableTimeout(const ByteString& targetIp)
    {
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        auto it = ndpCache.find(targetIp);
        if (it == ndpCache.end())
            return;

        global.timeManager.cancelTimer(it->second.timerId);
        it->second.timerId = 0;

        if (global.configs.ndp.refresh.load(std::memory_order_relaxed))
        {
            // Move to Probe instead of stale
            startNud(targetIp, it->second, lock);
        }
        else
        {
            // Transition between NUD states:
            it->second.state =  NudState::STALE;
        }
    }
    
    void Ndp::startNud(const ByteString& targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock)
    {
        // Nud probe limit check
        uint32_t maxNud = global.configs.ndp.nudLimit.load(std::memory_order_relaxed);
        if (currentNudProbes.load(std::memory_order_relaxed) >= maxNud)
        {
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                queuedNudProbes.insert(targetIp);
            }
            return; // Do not probe now, will retry later
        }

        // Proceed with probe
        entry.state = NudState::PROBE;
        currentNudProbes.fetch_add(1, std::memory_order_seq_cst);

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            nsRetryCount[targetIp] = 0;
            pendingRequests.insert(targetIp);
        }

        cacheLock.unlock();
        scheduleNeighborSolicitation(targetIp);
    }

    void Ndp::refreshNeighborEntry(const ByteString& targetIp)
    {
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        auto it = ndpCache.find(targetIp);
        if (it == ndpCache.end()) return;

        // Only refresh if still reachable
        if (it->second.state == NudState::REACHABLE)
        {
            startNud(targetIp, it->second, lock); // Start nud
        }
        else
        {
            it->second.timerId = 0; // Only clear timer if nothing is done
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
            icmp.options.emplace_back(
                Variable::ICMPv6::Option::source,
                ByteString("\x01", 1),
                *currentMac
            );
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

        char flags = 0;
        flags |= 0x80; // R = 1 (router)
        if (targetIp) flags |= 0x40; // S = 1 if solicited
        flags |= 0x20; // O = 1 (override)
        icmp.reserved = ByteString({ flags, 0x00, 0x00, 0x00}, 4);
        icmp.payload = targetIp ? *targetIp : currentInterface->configs.ipv6.getLocalAddress();

        IcmpV6Header::Option target;
        target.option = Variable::ICMPv6::Option::target;
        target.length = ByteString("\x01", 1); // 8 Bytes
        target.value = currentMac;
        
        icmp.options.push_back(std::move(target));

        packet.Layer3.push_back(std::move(icmp));

        return packet;
    }

    PacketInfo Ndp::routeSolicitation(const ByteString& currentMac)
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

    PacketInfo Ndp::routeAdvertisement(const ByteString& currentMac)
    {
        PacketInfo packet;
        IcmpV6Header icmp;

        icmp.type = Variable::ICMPv6::Type::ndpRouteAdvertisement;
        icmp.code = ByteString("\x00", 1);

        uint8_t flags = 0;
        if (configs.managedConfigFlag.load(std::memory_order_relaxed)) flags |= 0x80; // M-bit
        if (configs.otherConfigFlag.load(std::memory_order_relaxed)) flags |= 0x40; // O-bit

        // Preference bit
        switch (configs.preference.load(std::memory_order_relaxed))
        {
            case Configs::Preference::LOW: flags |= 0x18; break; // 01 << 3
            case Configs::Preference::HIGH: flags |= 0x08; break; // 10 << 3
            default: break; // MEDIUM is 00
        }

        // Build reserved field [1 byte TTL][1 byte Flags][2 byte router lifetime]
        icmp.reserved = Functions::numToByte(configs.raHopLimitUnspecified.load(std::memory_order_relaxed) ? 0 : 64, 1) +
            ByteString(1, flags) + 
            Functions::numToByte(configs.routerLifetime.load(std::memory_order_relaxed), 2);


        icmp.payload = Functions::numToByte(configs.reachableTime.load(std::memory_order_relaxed), 4) +
            Functions::numToByte(0, 4); // 0 means use your own timer

        IcmpV6Header::Option source;
        source.option = Variable::ICMPv6::Option::source;
        source.length = ByteString("\x01", 1);
        source.value = currentMac;
        icmp.options.push_back(std::move(source));

        if (!configs.mtuSuppress.load(std::memory_order_relaxed))
        {
            IcmpV6Header::Option mtu;
            mtu.option = Variable::ICMPv6::Option::mtu;
            mtu.length = ByteString("\x01", 1);
            mtu.value = ByteString("\x00\x00", 2) + // Reserved
            Functions::numToByte(currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed)); //TODO fix
            icmp.options.push_back(std::move(mtu));
        }

        if (configs.autoConfigPrefix.load(std::memory_order_relaxed))
        {
            std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
            for (const auto& addr : currentInterface->configs.ipv6.globalAddresses)
            {
                if (!addr->valid) continue;

                const uint8_t prefixLen = addr->prefix;
                uint8_t flags = 0;
                flags |= 0x80; // L = on-link
                flags |= 0x40; // A = autonomous

                uint32_t lifetime = configs.raLifetime.load(std::memory_order_relaxed);
                uint32_t preferredLifetime = configs.raPreferedLifetime.load(std::memory_order_relaxed);
                {
                    std::shared_lock<std::shared_mutex> lock(configs.configMutex);
                    if (configs.raIntervalMS)
                    {
                        lifetime /= 1000;
                        preferredLifetime /= 1000;
                    }
                }

                ByteString value;
                value += ByteString(1, prefixLen);
                value += ByteString(1, flags);
                value += ByteString("\x00\x00", 2);
                value += Functions::numToByte(lifetime, 4);
                value += Functions::numToByte(preferredLifetime, 4);
                value += ByteString(4, 0x00);
                value += Functions::computeNetworkAddress(addr->ip, prefixLen);

                icmp.options.emplace_back(
                    Variable::ICMPv6::Option::prefix,
                    ByteString("\x04", 1),
                    value
                );
            }
        }

        packet.Layer3.push_back(std::move(icmp));
        
        return packet;
    }

    void Ndp::sendNeighborAdvertisement(const ByteString& destMac, ByteString const* targetIp)
    {
        if (configs.suppressNA.load(std::memory_order_relaxed)) return;

        if (!targetIp || targetIp->empty()) return;
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            auto& lastTime = lastUnsolicitedNaTime[*targetIp];
            if (now - lastTime < std::chrono::seconds(1)) return;
            lastTime = now;
        }

        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            PacketInfo naPacket = neighborAdvertisement(iface.getMac(), targetIp);
            IPPacket::buildIp(currentInterface, naPacket, targetIp ? *targetIp : Variable::IPv6::multicast, nullptr, &destMac, 0, 255, Variable::IP::icmpv6);
        }
    }
    
    void Ndp::sendRouteSolicitation(const ByteString& targetIp)
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            PacketInfo rsPacket = routeSolicitation(iface.getMac());

            // Set the IP header and send the packet.
            IPPacket::buildIp(currentInterface, rsPacket, generateMulticastSolicitationAddress(targetIp), nullptr, nullptr, 0, 255, Variable::IP::icmpv6);
        }
    }

    void Ndp::sendRouteAdvertisement(const ByteString& targetMac, const ByteString& targetIp)
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            // Gather interface values.
            PacketInfo raPacket = routeAdvertisement(iface.getMac());
            
            // Set the IP header and send the packet.
            IPPacket::buildIp(currentInterface, raPacket, targetIp, nullptr, &targetMac, 0, 255, Variable::IP::icmpv6);
        }
    }

    void Ndp::sendRedirectMessage(const ByteString& targetIp, const ByteString& destinationIp)
    {
        if (!currentInterface || !currentInterface->routingInstance || !configs.redirects.load(std::memory_order_relaxed)) return;

        PacketInfo packet;
        IcmpV6Header icmp;

        icmp.type = Variable::ICMPv6::Type::ndpRedirectMessage;
        icmp.code = ByteString("\x00", 1);
        icmp.reserved = ByteString(4, 0x00);
        icmp.payload = destinationIp + targetIp;

        // Target Link-Layer Address (MAC of better next-hop)
        {
            std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
            icmp.options.emplace_back(
                Variable::ICMPv6::Option::target,
                ByteString("\x01", 1),
                currentInterface->configs.getMac()
            );
        }

        packet.Layer3.push_back(std::move(icmp));

        IPPacket::buildIp(
            currentInterface,
            packet,
            destinationIp,
            nullptr,
            nullptr,
            0, 255,
            Variable::IP::icmpv6
        );
    }

    void Ndp::sendRedirectIfNeeded(const PacketInfo& originalPacket)
    {
        //TODO move to packet forwarder
        if (!currentInterface || currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            return;

        // Ensure original packet is IPv6
        if (originalPacket.Layer3.empty() || !std::holds_alternative<IPv6Header>(originalPacket.Layer3[0]))
            return;

        const IPv6Header& ipHeader = std::get<IPv6Header>(originalPacket.Layer3[0]);

        // Destination must not be multicast
        if (Functions::isMulticast(ipHeader.destinationAddress) || Functions::isMulticast(ipHeader.sourceAddress))
            return;

        // Check if destination is already on-link (i.e., we know its mac from the same interface)
        ByteString* destMac = getMac(ipHeader.destinationAddress);
        if (!destMac) return;

        // Only send if we know the mac of the source (so we can unicast the redirect)
        ByteString* sourceMac = getMac(ipHeader.sourceAddress);
        if (!sourceMac) return;

        // Don't redirect if destination == next hop (i.e., source already has correct route)
        if (ipHeader.destinationAddress == ipHeader.sourceAddress)
            return;

        // Build the redirect message and send it back to the original sender
        sendRedirectMessage(ipHeader.destinationAddress, ipHeader.sourceAddress);
    }

    void Ndp::receiveRouteAdvertisement(const IcmpV6Header& receivedRA, const ByteString& sourceIp, const ByteString& sourceMac)
    {
        if (receivedRA.payload.size() < 8 || configs.suppressRA.load(std::memory_order_relaxed)) return;

        if (configs.destinationGuard.load(std::memory_order_relaxed))
        {
            Configs::RaGuardMode mode = configs.raGuardMode.load(std::memory_order_relaxed);

            if (mode == Configs::RaGuardMode::BLOCK_ALL)
                return;

            if (mode == Configs::RaGuardMode::MAC_WHITELIST && !raGuardAllowedMacs.count(sourceMac))
                return;
            
            if (mode != Configs::RaGuardMode::TRUSTED)
            {
                auto now = std::chrono::steady_clock::now();

                auto& lastTime = raReceivedTimestamps[sourceMac];
                auto rateLimit = configs.raRateLimit.load(std::memory_order_relaxed);
                auto interval = std::chrono::milliseconds(1000 / std::max(rateLimit, 1u));

                if (now - lastTime < interval)
                {
                    return; // Drop RA as too fast
                }

                lastTime = now;
            }
        }

        // Parse M/O flags and preference bits (do NOT overwrite config)
        uint16_t routerLifetime = 0;
        bool mFlag = false, oFlag = false;
        if (receivedRA.reserved.size() >= 4)
        {
            uint8_t flags = receivedRA.reserved[1];
            routerLifetime = Functions::byteToNum(receivedRA.reserved.substr(2, 2));

            mFlag = flags & 0x80;
            oFlag = flags & 0x40;

            uint8_t prfBits = (flags >> 3) & 0b11;
            switch (prfBits)
            {
                case 1: configs.preference.store(Configs::Preference::LOW, std::memory_order_relaxed); break;
                case 2: configs.preference.store(Configs::Preference::HIGH, std::memory_order_relaxed); break;
                default: break; // Leave as MEDIUM
            }
        }

        if (configs.autoConfigDefaultRoute.load(std::memory_order_relaxed) && routerLifetime > 0 && sourceIp.size() == 16)
        {
            if (global.configs.ndp.ndAsRouteOwner.load(std::memory_order_relaxed))
            {
                //TODO make ndp interface owner
            }
            //TODO add default route
        }

        // Process each RA option (only prefix and mtu)
        for (const auto& opt : receivedRA.options)
        {
            if (opt.option == Variable::ICMPv6::Option::prefix && opt.value.size() >= 32)
            {
                uint8_t prefixLen = opt.value[0];
                uint8_t prefixFlags = opt.value[1];
                bool A = prefixFlags & 0x40;

                uint32_t validLifetime = Functions::byteToNum(opt.value.substr(4, 4));
                uint32_t preferredLifetime = Functions::byteToNum(opt.value.substr(8, 4));

                if (preferredLifetime > validLifetime) continue;

                ByteString prefix = opt.value.substr(16, 16);

                // Check exclusion
                bool isExcluded = std::any_of(slaacExclusionPrefixes.begin(), slaacExclusionPrefixes.end(), [&](const ByteString& p) {
                    return Functions::computeNetworkAddress(prefix, prefixLen) ==
                           Functions::computeNetworkAddress(p, prefixLen);
                });
                if (isExcluded) continue;

                // SLAAC (only if explicitly enabled by config)
                if (A && validLifetime > 0 && preferredLifetime <= validLifetime && configs.slaacEnabled.load(std::memory_order_relaxed))
                {
                    InterfaceConfigs::IPv6State::IPv6Address* slaacAddr = new InterfaceConfigs::IPv6State::IPv6Address();
                    slaacAddr->prefix = prefixLen;
                    slaacAddr->tentative = true;
                    slaacAddr->valid = false;
                    slaacAddr->globalTentative = true;
                    slaacAddr->globalValid = false;
                    slaacAddr->deprecated = false;
                    slaacAddr->preferredLifetime = preferredLifetime;

                    slaacAddr->ip = Functions::calculateEui64(prefix, currentInterface->configs.getMac(), prefixLen);

                    {
                        std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                        currentInterface->configs.ipv6.globalAddresses.push_back(slaacAddr);
                    }

                    slaacAddr->expirationId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::seconds(validLifetime),
                        [this, slaacAddr]()
                        {
                            std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                            slaacAddr->globalValid = false;
                            slaacAddr->expirationId = 0;
                        }
                    );

                    slaacAddr->preferedExpirationId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::seconds(preferredLifetime),
                        [this, slaacAddr]()
                        {
                            std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                            slaacAddr->deprecated = true;
                            slaacAddr->preferedExpirationId = 0;
                        }
                    );

                    duplicateAddressDetection(slaacAddr, false);
                }
            }
        }
    }

    void Ndp::receiveRedirectMessage(const IcmpV6Header& redirect, const ByteString& sourceIp)
    {
        if (redirect.payload.size() < 32) return;

        ByteString destinationIp = redirect.payload.substr(0, 16);
        ByteString betterNextHop = redirect.payload.substr(16, 16);
        ByteString nextHopMac;

        for (const auto& opt : redirect.options)
        {
            if (opt.option == Variable::ICMPv6::Option::target && opt.value.size() == 6)
            {
                nextHopMac = opt.value;
                break;
            }
        }

        if (!nextHopMac.empty())
        {
            addNdpEntry(betterNextHop, nextHopMac);
        }
    }

    void Ndp::duplicateAddressDetection(InterfaceConfigs::IPv6State::IPv6Address* addr, bool isLinkLocal)
    {
        if (global.configs.nsfActive.load(std::memory_order_relaxed))
        {
            auto now = std::chrono::steady_clock::now();
            auto suppressWindow = std::chrono::seconds(global.configs.ndp.nsfDadSupressionTime.load(std::memory_order_relaxed));

            if (now - global.configs.nsfStartTime < suppressWindow)
            {
                // Within NFS window - check resolution throttle
                uint32_t maxRes = global.configs.ndp.nsfThrottleResolutions.load(std::memory_order_relaxed);
                if (nfsResolutionCount.fetch_add(1, std::memory_order_seq_cst) >= maxRes)
                {
                    return; // Drop new resolution due to throttle limit
                }
                ByteString ipCopy = addr->ip;
                std::lock_guard<std::mutex> lock(requestMutex);
                if (!pendingDadReschedules.count(ipCopy))
                {
                    pendingDadReschedules.emplace(
                        ipCopy,
                        global.timeManager.addTimer(
                            global.configs.nsfStartTime + suppressWindow,
                            [this, addr, isLinkLocal]() {
                                pendingDadReschedules.erase(addr->ip);
                                duplicateAddressDetection(addr, isLinkLocal);
                            }
                        )
                    );
                }
                return; // Supress DAD during NSF recovery
            }
        }

        if (currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            return;

        if (configs.dadAttempts.load(std::memory_order_relaxed) == 0)
            return;

        // If DAD is disabled or address is already marked non-tentative
        if (!addr->tentative)
            return;

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            nsRetryCount[addr->ip] = 0;
        }

        // Clear existing entry in the cache (DAD must be clean)
        {
            std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
            ndpCache.erase(addr->ip);
        }

        const ByteString ipCopy = addr->ip;

        // Register traching for replies
        {
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            neighborReplyStatus[ipCopy] = false;
        }

        // Start DAD
        preformDad(addr, isLinkLocal);
    }

    void Ndp::preformDad(InterfaceConfigs::IPv6State::IPv6Address* addr, bool isLinkLocal)
    {
        // Local capture values
        auto& iface = currentInterface->configs;
        const ByteString ipCopy = addr->ip;
        const int maxAttempts = configs.dadAttempts.load(std::memory_order_relaxed);
        const auto delay = std::chrono::milliseconds(configs.dadTime.load(std::memory_order_relaxed));

        int attempt = nsRetryCount[ipCopy];
        // Check if reply was received
        bool isDuplicate = false;
        {
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            if (neighborReplyStatus.count(ipCopy))
            {
                isDuplicate = neighborReplyStatus[ipCopy];
            }
        }

        bool remove = false;
        if (isDuplicate)
        {
            {
                std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                addr->tentative = false;
                addr->valid = false;
            }
            currentInterface->markAddressDuplicate(ipCopy, isLinkLocal);
            remove = true;
        }
        else if (attempt >= maxAttempts)
        {
            std::unique_lock<std::shared_mutex> lock(iface.ipMutex);
            addr->tentative = false;
            addr->valid = true;
            remove = true;
        }
        else
        {
            // Send anonymous NS (source = ::, no MAC option)
            PacketInfo ns = neighborSolicitation(ipCopy, nullptr);
            IPPacket::buildIp(
                currentInterface, ns,
                generateMulticastSolicitationAddress(ipCopy),
                &Variable::IPv6::source,
                nullptr, 0, 255, Variable::IP::icmpv6
            );

            pendingRequests.insert(ipCopy);
            nsRetryCount[addr->ip]++;

            uint32_t timerId = global.timeManager.addTimer(
                std::chrono::steady_clock::now() + delay,
                [this, addr, isLinkLocal]() {
                    preformDad(addr, isLinkLocal);
                }
            );
            {
                std::lock_guard<std::mutex> lock(requestMutex); // reuse existing mutex
                dadTimers[ipCopy] = timerId;
            }

        }

        if (remove)
        {
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                pendingRequests.erase(ipCopy);
                nsRetryCount.erase(ipCopy);
                dadTimers.erase(ipCopy);
            }

            {
                std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
                neighborReplyStatus.erase(ipCopy);
            }
        }

    }

    void Ndp::scheduleNextRA()
    {
        if (!running.load(std::memory_order_relaxed)) return;
        auto now = std::chrono::steady_clock::now();
        for (auto it = raReceivedTimestamps.begin(); it != raReceivedTimestamps.end();)
        {
            if (now - it->second > std::chrono::minutes(10))
            {
                it = raReceivedTimestamps.erase(it);
            }
            else
            {
                ++it;
            }
        }
        
        uint32_t* raTimerId = new uint32_t();

        uint32_t baseInterval;
        {
            std::shared_lock<std::shared_mutex> lock(configs.configMutex);
            baseInterval = configs.raInterval;
            if (configs.advertisementInterval)
            {
                uint8_t min = configs.raIntervalMin;
                if (min > baseInterval) min = baseInterval;
                uint32_t delta = baseInterval - min;
                baseInterval = min + (rand() % (delta + 1));
            }
        }
        *raTimerId = global.timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(baseInterval),
            [this, raTimerId]()
            {
                if (!running.load(std::memory_order_relaxed) ||
                    configs.suppressRA.load(std::memory_order_relaxed))
                    return;
                
                auto& iface = currentInterface->configs;
                ByteString ip = iface.ipv6.getLocalAddress();

                sendRouteAdvertisement(Variable::Mac::broadcast, ip);

                // Reschedule next RA
                raTimerIds.erase(*raTimerId);
                delete raTimerId;
                scheduleNextRA();
            }
        );

        raTimerIds.insert(*raTimerId);
    }

    void Ndp::scheduleNeighborSolicitation(const ByteString& targetIp)
    {
        if (!running.load(std::memory_order_relaxed)) return;

        bool runningNud = false; // Is NUD running

        uint8_t attempt = nsRetryCount[targetIp];
        uint8_t maxAttempts = 3; // Default max retries for NS

        NdpCacheEntry* entry = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(configs.configMutex); // Lock for nud values, this makes all nud values thread safe at the same time for reconfiguration.
            {
                std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
                auto it = ndpCache.find(targetIp);
                if (it != ndpCache.end())
                {
                    if (it->second.state == NudState::PROBE || it->second.state == NudState::UNREACHABLE)
                    {
                        // Running NUD
                        runningNud = true;
                        maxAttempts = configs.nudBaseAttempts;
                    }
                    entry = &it->second;
                }
            }
            
            if (attempt >= maxAttempts)
            {
                {
                    std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
                    if (runningNud && entry && entry->nudGroup <= configs.nudBase)
                    {
                        entry->state = NudState::UNREACHABLE;
                        uint32_t finalWait = configs.nudFinalWait;
                        entry->nudRetryTimerId = global.timeManager.addTimer(
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(finalWait),
                            [this, targetIp]() { retryNud(targetIp); }
                        );

                        currentNudProbes.fetch_sub(1, std::memory_order_seq_cst);

                        ByteString retryIp;
                        {
                            std::lock_guard<std::mutex> requestLock(requestMutex);
                            if (!queuedNudProbes.empty())
                            {
                                retryIp = *queuedNudProbes.begin();
                                queuedNudProbes.erase(retryIp);
                            }
                        }
                        if (!retryIp.empty())
                        {
                            auto it = ndpCache.find(retryIp);
                            if (it != ndpCache.end())
                            {
                                startNud(retryIp, it->second, lock);
                            }
                        }
                    }
                    else
                    {
                        ndpCache.erase(targetIp);
                        std::erase(insertionOrder, targetIp);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    pendingRequests.erase(targetIp);
                    nsRetryCount.erase(targetIp);
                    nsRetryTimers.erase(targetIp);
                }

                {
                    std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
                    neighborReplyStatus.erase(targetIp);
                }

                return;
            }
        }

        // Send the solicitation
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            ByteString mac = iface.getMac();
            PacketInfo nsPacket = neighborSolicitation(targetIp, &mac);
            IPPacket::buildIp(currentInterface, nsPacket, generateMulticastSolicitationAddress(targetIp), nullptr, nullptr, 0, 255, Variable::IP::icmpv6);
        }

        // Reschedule retry
        nsRetryCount[targetIp]++;
        uint16_t nudBaseInterval;
        if (runningNud)
        {
            std::shared_lock<std::shared_mutex> lock(configs.configMutex);
            nudBaseInterval = configs.nudBaseInterval;
        }

        global.timeManager.cancelTimer(nsRetryTimers[targetIp]);
        nsRetryTimers[targetIp] = global.timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(runningNud 
                ? nudBaseInterval
                : configs.nsInterval.load(std::memory_order_relaxed)),
            [this, targetIp]()
            {
                {
                    std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
                    if (neighborReplyStatus.count(targetIp) && neighborReplyStatus[targetIp])
                    {
                        std::lock_guard<std::mutex> lock(requestMutex);
                        pendingRequests.erase(targetIp);
                        nsRetryCount.erase(targetIp);
                        return;
                    }
                }
                scheduleNeighborSolicitation(targetIp);
            }
        );
    }

    void Ndp::retryNud(const ByteString& targetIp)
    {
        if (!running.load(std::memory_order_relaxed)) return;

        {
            std::shared_lock<std::shared_mutex> lock(ndpCacheMutex);
            auto it = ndpCache.find(targetIp);
            if (it == ndpCache.end()) return;

            // If the neighbor was removed or transitioned, stop
            if (it->second.state != NudState::UNREACHABLE)
                return;

            {
                std::lock_guard<std::mutex> lock(requestMutex);
                nsRetryCount[targetIp] = 0;
                pendingRequests.insert(targetIp);
            }
            it->second.nudGroup++;
        }

        // Retry NUD: send NS
        sendNeighborSolicitation(targetIp);
    }
    
    void Ndp::addSlaacExclusionPrefix(const ByteString& prefix, bool remove)
    {
        if (remove)
        {
            std::erase_if(slaacExclusionPrefixes, [&](ByteString& pref) { return pref == prefix; });
        }
        else if (!std::any_of(
            slaacExclusionPrefixes.begin(),
            slaacExclusionPrefixes.end(),
            [&](ByteString& pref) { return pref == prefix; })
        )
        {
            slaacExclusionPrefixes.push_back(prefix);
        }
    }
    
    void Ndp::addRaGuardAllowedMac(const ByteString& mac, bool remove)
    {
        if (remove)
        {
            raGuardAllowedMacs.erase(mac);
        }
        else
        {
            raGuardAllowedMacs.insert(mac);
        }
    }

    bool Ndp::shouldLog()
    {
        auto now = std::chrono::steady_clock::now();
        uint16_t rate = configs.loggingRate.load(std::memory_order_relaxed);
        if (rate == 0) return true;
        
        auto minInterval = std::chrono::microseconds(1'000'000 / rate);

        if (now - lastLogWindowStart.load(std::memory_order_relaxed) >= minInterval)
        {
            lastLogWindowStart.store(now, std::memory_order_release);
            return true;
        }
        return false;
    }
}
