// TODO naglean
// TODO nudigp
#include <Ndp.h>
#include <Functions.h>
#include <IPPacket.h>
#include <TimeManager.h>
#include <VirtualRouter.h>
#include <Global.h>
#include <PacketBuilder.hpp>
#include <Ethernet.h>

namespace Protocol
{
    // Constructor: Initiates the NDP object with the given interface
    Ndp::Ndp(Interface& iface)
        : currentInterface(&iface),
        global(iface.getVRF()->global)
    {
        // Initialize global configs
        configs.refresh = global.configs.ndp.refresh.load(std::memory_order_relaxed);
        configs.loggingRate = global.configs.ndp.loggingRate.load(std::memory_order_relaxed);
        configs.cacheExpire = global.configs.ndp.cacheExpire.load(std::memory_order_relaxed);
        configs.dadTime = global.configs.ndp.dadTime.load(std::memory_order_relaxed);
        configs.reachableTime = global.configs.ndp.reachableTime.load(std::memory_order_relaxed);
        configs.interfaceLimit = global.configs.ndp.interfaceLimit.load(std::memory_order_relaxed);

        if (global.routingEnabled)
            initializeNdp();
    }

    void Ndp::initializeNdp()
    {
        // Add static neighbors
        std::shared_lock<std::shared_mutex> lock(global.configs.ndp.neighborMutex);
        for (const auto& [ip, neighbor] : global.configs.ndp.neighbors)
        {
            if (neighbor.interface == currentInterface->configs.key)
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

    void Ndp::addNdpEntry(const IPAddress& targetIp, uint64_t targetMac, bool proxy, bool isStatic)
    {
        if (!isStatic)
        {
            std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
            uint32_t limit = configs.interfaceLimit.load(std::memory_order_relaxed);
            if (limit != 0 && ndpCache.size() >= limit)
            {
                // Remove an entry to enforce a limit.
                IPAddress evicted = insertionOrder.front();
                insertionOrder.erase(insertionOrder.begin());
                global.timeManager.cancelTimer(ndpCache[evicted].timerId);
                ndpCache.erase(evicted);
            }
        }
        NdpCacheEntry entry;
        writeU48(entry.macAddress, targetMac);
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
        uint8_t mac[6];
        writeU48(mac, targetMac);
        processQueuedPackets(targetIp, mac);
    }

    // Get MAC address for the given ip
    uint8_t* Ndp::getMac(uint8_t* out, const uint8_t* ip)
    {
        IPAddress address;
        address.isV6 = true;
        std::memcpy(address.raw, ip, 16);
        std::shared_lock<std::shared_mutex> lock(ndpCacheMutex);
        {

            std::shared_lock<std::shared_mutex> neighborLock(global.configs.ndp.neighborMutex);
            auto staticIt = staticNdpCache.find(address);
            if (staticIt != staticNdpCache.end())
            {
                memcpy(out, staticIt->second.macAddress, 6);
                return out;
            }
        }
        auto it = ndpCache.find(address);
        if (it != ndpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime)
        {
            memcpy(out, it->second.macAddress, 6);
            return out;
        }
        return nullptr;
    }

    void Ndp::initiateSlaac()
    {
        if (!configs.slaacEnabled.load(std::memory_order_relaxed))
            return;

        // Send a Router Solicitation to get fresh RA with A-bit prefixes
        auto& iface = currentInterface->configs;
        uint8_t mac[6];
        iface.getMac(mac);
        PacketBuilder rs(currentInterface);
        routeSolicitation(rs, mac);

        IPPacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = rs,
            .destIp = ICMPV6_ALL_ROUTERS,
            .protocolType = IP_ICMPV6
        };

        IPPacket::buildIpv6(build);
    }

    void Ndp::resolveAndSend(const uint8_t* targetIp, PacketBuilder& packetToSend)
    {
        IPAddress ip;
        ip.isV6 = true;
        std::memcpy(ip.raw, targetIp, 16);

        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp[ip].emplace(packetToSend);
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
        auto it = ndpCache.find(ip);
        if (it != ndpCache.end())
        {
            bool strict = global.configs.ndp.strictMode;

            if (it->second.state == NudState::STALE)
            {
                startNud(ip, it->second, lock);
            }
            else if (it->second.state == NudState::DELAY)
            {
                if (strict)
                {
                    // Delay isn't trusted in strict mode
                    startNud(ip, it->second, lock);
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
                    startNud(ip, it->second, lock);
                }
                else
                {
                    lock.unlock();
                    processQueuedPackets(ip, it->second.macAddress);
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
                queuedResolution.insert(ip);
                return;
            }

            currentResolvingNeighbors.fetch_add(1, std::memory_order_seq_cst);

            lock.unlock();
            sendNeighborSolicitation(ip); // Initial Learning
        }
    }

    void Ndp::sendNeighborSolicitation(const IPAddress& targetIp)
    {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            if (pendingRequests.count(targetIp)) return;
            pendingRequests.insert(targetIp);
            nsRetryCount[targetIp] = 0;
        }
        scheduleNeighborSolicitation(targetIp);
    }

    void Ndp::receiveNeighborAdvertisement(const Icmpv6Header& receivedNA, const uint8_t* sourceIp)
    {
        auto trail = receivedNA.getTrail();
        uint8_t mac[6];
        bool macFound = false;
        IPAddress targetIp;
        targetIp.isV6 = true;
        std::memcpy(targetIp.raw, trail.data(), 16);

        std::vector<TLV8Option> options;
        parseIcmpv6Options(trail.data() + 16, trail.size() - 16, options);

        for (const auto& opt : options)
        {
            // Extract MAC from options
            if (opt.type == ICMPV6_OPTION_NDP_TARGET && opt.valueSize == 6)
            {
                std::memcpy(mac, opt.value, 6);
                macFound = true;
                break;
            }
        }
        if (!macFound) return;

        {
            {
                std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                for (const auto& addr : currentInterface->configs.ipv6.globalAddresses)
                {
                    if (std::memcmp(addr->ip, trail.data(), 16) == 0 && addr->tentative && std::memcmp(sourceIp, &IPV6_SOURCE, 16) == 0)
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
                    std::memcpy(it->second.macAddress, mac, 6);
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
                    std::memcpy(entry.macAddress, mac, 6);
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
                sendNeighborSolicitation(*queuedResolution.begin());
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

    void Ndp::receiveNeighborSolicitation(const Icmpv6Header& nsHeader, const uint8_t* srcIp, const uint8_t* srcMac)
    {
        auto trail = nsHeader.getTrail();
        IPAddress targetIp;
        targetIp.isV6 = true;
        std::memcpy(targetIp.raw, trail.data(), 16);
        uint8_t replyMac[6];

        bool isOwned = false;
        bool isProxy = false;

        {
            if (currentInterface->configs.hasAddress(trail.data()))
            {
                currentInterface->configs.getMac(replyMac);
                isOwned = true;
            }
            else if (proxyEntries.count(targetIp))
            {
                writeU48(replyMac, proxyEntries[targetIp]);
                isProxy = true;
            }
        }

        if (!isOwned && !isProxy) return;

        // Always respond to DAD (unspecified source IP = DAD probe)
        //the S flag will be 0 in this case (unsolicited NA)
        PacketBuilder na(currentInterface);
        neighborAdvertisement(na, replyMac, isProxy ? trail.data() : srcIp);

        if (srcMac && std::memcmp(srcIp, &IPV6_SOURCE, 16) == 0)
        {
            // Multicast NA for DAD response or missing MAC
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = na,
                .destIp = IPV6_MULTICAST,
                .sourceIp = trail.data(),
                .protocolType = IP_ICMPV6
            };

            IPPacket::buildIpv6(build);
        }
        else
        {
            // Unicast NA back to sender
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = na,
                .destIp = srcIp,
                .sourceIp = trail.data(),
                .destMac = srcMac,
                .protocolType = IP_ICMPV6
            };

            IPPacket::buildIpv6(build);
        }
    }

    void Ndp::processQueuedPackets(const IPAddress& targetIp, const uint8_t* macAddress)
    {
        std::queue<PacketBuilder> packets;
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            if (auto que = packetQueuePerIp.extract(targetIp))
            {
                packets = std::move(que.mapped());
            }
        }

        while (!packets.empty())
        {
            PacketBuilder pkt = std::move(packets.front());
            packets.pop();
            auto current = pkt.previewNextBuildHeader();
            if (!current) continue;

            // Continue building next header
            switch (current->type)
            {
                //TODO add more headers
                case HeaderType::ETHERNET:
                    Protocol::Ethernet::build(currentInterface, pkt, targetIp.raw, macAddress, ETHERNET_IPV6);
                    break;
                default:
                    continue;
            }
        }
    }

    void Ndp::onReachableTimeout(const IPAddress& targetIp)
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
    
    void Ndp::startNud(const IPAddress& targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock)
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

    void Ndp::refreshNeighborEntry(const IPAddress& targetIp)
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
    
    uint8_t* Ndp::generateMulticastSolicitationAddress(uint8_t* out, const uint8_t* targetIp)
    {
        if (!out || !targetIp)
        {
            return nullptr;
        }
        std::memcpy(out, ICMPV6_SOLICIT_MULTICAST, 16);
        out[13] = targetIp[13];
        out[14] = targetIp[14];
        out[15] = targetIp[15];
        return out;
    }

    void Ndp::neighborSolicitation(PacketBuilder& packet, const IPAddress& targetIp, const uint8_t* currentMac)
    {
        IPPacket::reserveIpv6(packet);
        packet.reserveHeader(HeaderType::ICMPV6, 0); // Will set size later

        Icmpv6Header icmp;
        
        BuildEntry* nextHeader = packet.nextBuildHeader();
        if (!nextHeader || nextHeader->type != HeaderType::ICMPV6)
            return;

        icmp.setBuffer(nextHeader->buffer);

        icmp.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
        icmp.setCode(0);
        icmp.setReservedInt(0);

        uint8_t* trail = icmp.getTrailData();
        std::memcpy(trail, targetIp.raw, 16);

        if (currentMac)
        {
            TLV8BufferManager options(trail + 16, 8);
            options.append(ICMPV6_OPTION_NDP_TARGET, 1, currentMac, 6);
            nextHeader->length = Icmpv6Header::fixedSize + options.size();
        }

        packet.bufferOffset += nextHeader->length;
    }

    void Ndp::neighborAdvertisement(PacketBuilder& packet, const uint8_t* currentMac, const uint8_t* targetIp)
    {
        IPPacket::reserveIpv6(packet);
        packet.reserveHeader(HeaderType::ICMPV6, 0); // Will set size later

        Icmpv6Header icmp;

        BuildEntry* nextHeader = packet.nextBuildHeader();
        if (!nextHeader || nextHeader->type != HeaderType::ICMPV6)
            return;

        icmp.setBuffer(nextHeader->buffer);

        icmp.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
        icmp.setCode(0);
        
        uint8_t reserved[4];
        std::fill(reserved, reserved + 4, 0);
        reserved[0] = 0x80 | (targetIp ? 0x40 : 0) | 0x20;
        icmp.setReserved(reserved);

        uint8_t* trail = icmp.getTrailData();
        if (targetIp)
        {
            std::memcpy(trail, targetIp, 16);
        }
        else
        {
            currentInterface->configs.ipv6.getLocalAddress(trail);
        }

        TLV8BufferManager options(trail + 16, 8);
        options.append(ICMPV6_OPTION_NDP_TARGET, 1, currentMac, 6);
    }

    void Ndp::routeSolicitation(PacketBuilder& packet, const uint8_t* currentMac)
    {
        IPPacket::reserveIpv6(packet);
        packet.reserveHeader(HeaderType::ICMPV6, 0); // Will set size later

        Icmpv6Header icmp;

        BuildEntry* nextHeader = packet.nextBuildHeader();
        if (!nextHeader || nextHeader->type != HeaderType::ICMPV6)
            return;

        icmp.setBuffer(nextHeader->buffer);

        icmp.setType(ICMPV6_OPCODE_NDP_ROUTE_SOLICITATION);
        icmp.setCode(0);
        icmp.setReservedInt(0);
        
        // Get trail pointer
        uint8_t* trail = icmp.getTrailData();
        
        TLV8BufferManager options(trail, 8);
        options.append(ICMPV6_OPTION_NDP_SOURCE, 1, currentMac, 6);

        nextHeader->length = Icmpv6Header::fixedSize + options.size();
        packet.bufferOffset += nextHeader->length;
    }

    void Ndp::routeAdvertisement(PacketBuilder& packet, const uint8_t* currentMac)
    {
        IPPacket::reserveIpv6(packet);
        packet.reserveHeader(HeaderType::ICMPV6, 0); // Will set size later

        Icmpv6Header icmp;

        BuildEntry* nextHeader = packet.nextBuildHeader();
        if (!nextHeader || nextHeader->type != HeaderType::ICMPV6)
            return;

        icmp.setBuffer(nextHeader->buffer);

        icmp.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
        icmp.setCode(0);

        uint8_t reserved[4];
        std::fill(reserved, reserved + 4, 0);
        if (configs.managedConfigFlag.load(std::memory_order_relaxed)) reserved[1] |= 0x80; // M-bit
        if (configs.otherConfigFlag.load(std::memory_order_relaxed)) reserved[1] |= 0x40; // O-bit

        // Preference bit
        switch (configs.preference.load(std::memory_order_relaxed))
        {
            case Configs::Preference::LOW: reserved[1] |= 0x00180000; break; // 01 << 3
            case Configs::Preference::HIGH: reserved[1] |= 0x00080000; break; // 10 << 3
            default: break; // MEDIUM is 00
        }
        
        reserved[0] |= configs.raHopLimitUnspecified.load(std::memory_order_relaxed) ? 0 : 64;
        writeU16(reserved + 2, configs.routerLifetime.load(std::memory_order_relaxed));
        icmp.setReserved(reserved);

        uint8_t* trail = icmp.getTrailData();
        writeU32(trail, configs.reachableTime.load(std::memory_order_relaxed));
        writeU32(trail + 4, 0); // 0 means use your own timer

        TLV8BufferManager options(trail + 8);
        options.append(ICMPV6_OPTION_NDP_SOURCE, 1, currentMac, 6);

        if (!configs.mtuSuppress.load(std::memory_order_relaxed))
        {
            uint8_t mtu[6];
            writeU16(mtu, 0); // Reserved
            writeU32(mtu + 2, currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed));
            options.append(ICMPV6_OPTION_NDP_MTU, 1, mtu, 6);
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
                uint32_t preferredLifetime = configs.raPreferredLifetime.load(std::memory_order_relaxed);
                {
                    std::shared_lock<std::shared_mutex> lock(configs.configMutex);
                    if (configs.raIntervalMS)
                    {
                        lifetime /= 1000;
                        preferredLifetime /= 1000;
                    }
                }

                uint8_t value[30];
                value[0] = prefixLen;
                value[2] = flags;
                writeU16(value + 2, 0);
                writeU32(value + 4, lifetime);
                writeU32(value + 8, preferredLifetime);
                writeU32(value + 12, 0);
                Functions::computeNetworkAddress(value + 16, addr->ip, prefixLen, AddressFamily::IPv6);

                options.append(ICMPV6_OPTION_NDP_PREFIX, 4, value, 30);
            }
        }
        nextHeader->length = Icmpv6Header::fixedSize + options.size();
        packet.bufferOffset += nextHeader->length;
    }

    void Ndp::sendNeighborAdvertisement(const uint8_t* destMac, const uint8_t* targetIp)
    {
        if (configs.suppressNA.load(std::memory_order_relaxed)) return;

        if (!targetIp) return;
        auto now = std::chrono::steady_clock::now();
        {
            IPAddress ip;
            ip.isV6 = true;
            std::memcpy(ip.raw, targetIp, 16);
            std::lock_guard<std::mutex> lock(requestMutex);
            auto& lastTime = lastUnsolicitedNaTime[ip];
            if (now - lastTime < std::chrono::seconds(1)) return;
            lastTime = now;
        }

        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            PacketBuilder naPacket(currentInterface);
            uint8_t mac[6];
            neighborAdvertisement(naPacket, iface.getMac(mac), targetIp);

            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = naPacket,
                .destIp = targetIp ? targetIp : IPV6_MULTICAST,
                .destMac = destMac,
                .protocolType = IP_ICMPV6
            };

            IPPacket::buildIpv6(build);
        }
    }
    
    void Ndp::sendRouteSolicitation(const uint8_t* targetIp)
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            uint8_t mac[6];
            PacketBuilder rsPacket(currentInterface);
            routeSolicitation(rsPacket, iface.getMac(mac));

            // Set the IP header and send the packet.
            uint8_t multicastIp[16];

            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = rsPacket,
                .destIp = generateMulticastSolicitationAddress(multicastIp, targetIp),
                .protocolType = IP_ICMPV6
            };

            IPPacket::buildIpv6(build);
        }
    }

    void Ndp::sendRouteAdvertisement(const uint8_t* targetMac, const uint8_t* targetIp)
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& iface = currentInterface->configs;
            // Gather interface values.
            PacketBuilder raPacket(currentInterface);
            uint8_t mac[6];
            routeAdvertisement(raPacket, iface.getMac(mac));
            
            // Set the IP header and send the packet.
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = raPacket,
                .destIp = targetIp,
                .destMac = targetMac,
                .protocolType = IP_ICMPV6
            };

            IPPacket::buildIpv6(build);
        }
    }

    void Ndp::sendRedirectMessage(const uint8_t* targetIp, const uint8_t* destinationIp)
    {
        if (!currentInterface || !currentInterface->getVRF() || !configs.redirects.load(std::memory_order_relaxed)) return;

        PacketBuilder packet(currentInterface);

        IPPacket::reserveIpv6(packet);
        packet.reserveHeader(HeaderType::ICMPV6, 0); // Will set size later

        Icmpv6Header icmp;

        BuildEntry* nextHeader = packet.nextBuildHeader();
        if (!nextHeader || nextHeader->type != HeaderType::ICMPV6)
            return;
        
        icmp.setBuffer(nextHeader->buffer);

        icmp.setType(ICMPV6_OPCODE_NDP_REDIRECT_MESSAGE);
        icmp.setCode(0);
        icmp.setReserved(0);
        
        uint8_t* trail = icmp.getTrailData();
        
        std::memcpy(trail, destinationIp, 16);
        std::memcpy(trail + 16, targetIp, 16);
        
        TLV8BufferManager options(trail + 32, 8);
        options.append(ICMPV6_OPTION_NDP_TARGET, 1, 0, 0);
        currentInterface->configs.getMac(trail + 34);

        nextHeader->length = Icmpv6Header::fixedSize + 32 + options.size();
        packet.bufferOffset += nextHeader->length;

        IPPacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = packet,
            .destIp = destinationIp,
            .protocolType = IP_ICMPV6
        };

        IPPacket::buildIpv6(build);
    }

    void Ndp::sendRedirectIfNeeded(const PacketInfo& originalPacket, const uint8_t* pkt)
    {
        //TODO move to packet forwarder
        if (!currentInterface || currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            return;

        // Ensure original packet is IPv6
        IPv6HeaderRaw* ipv6 = nullptr;
        auto headers = originalPacket.headers;
        for (size_t i = 0; i < originalPacket.count; ++i)
        {
            auto header = headers[i];
            if (header.type == HeaderType::IPV6)
            {
                ipv6 = reinterpret_cast<IPv6HeaderRaw*>(const_cast<uint8_t*>(pkt) + header.offset);
            }
        }

        if (!ipv6) return;
/*
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
*/
    }

    void Ndp::receiveRouteAdvertisement(const Icmpv6Header& receivedRA, const uint8_t* sourceIp, const uint8_t* sourceMac)
    {
        if (configs.suppressRA.load(std::memory_order_relaxed)) return;

        if (configs.destinationGuard.load(std::memory_order_relaxed))
        {
            Configs::RaGuardMode mode = configs.raGuardMode.load(std::memory_order_relaxed);

            if (mode == Configs::RaGuardMode::BLOCK_ALL)
                return;

            if (mode == Configs::RaGuardMode::MAC_WHITELIST && !raGuardAllowedMacs.count(readU48(sourceMac)))
                return;
            
            if (mode != Configs::RaGuardMode::TRUSTED)
            {
                auto now = std::chrono::steady_clock::now();

                auto& lastTime = raReceivedTimestamps[readU48(sourceMac)];
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
        uint8_t flags = receivedRA.getReserved()[1];
        routerLifetime = readU32(receivedRA.getReserved() + 2);

        mFlag = flags & 0x80;
        oFlag = flags & 0x40;

        uint8_t prfBits = (flags >> 3) & 0b11;

        if (configs.autoConfigDefaultRoute.load(std::memory_order_relaxed) && routerLifetime > 0 && memcmp(sourceIp, IPV6_SOURCE, 16) != 0)
        {
            if (global.configs.ndp.ndAsRouteOwner.load(std::memory_order_relaxed))
            {
                //TODO make ndp interface owner
            }
            //TODO add default route
        }

        std::vector<TLV8Option> options;
        auto trail = receivedRA.getTrail();
        parseIcmpv6Options(trail.data() + 8, trail.size() - 8, options);

        // Process each RA option (only prefix and mtu)
        for (const auto& opt : options)
        {
            if (opt.type == ICMPV6_OPTION_NDP_PREFIX && opt.valueSize >= 30)
            {
                uint8_t prefixLen = opt.value[0];
                uint8_t prefixFlags = opt.value[1];
                bool A = prefixFlags & 0x40;

                uint32_t validLifetime = readU32(opt.value + 2);
                uint32_t preferredLifetime = readU32(opt.value + 6);

                if (preferredLifetime > validLifetime) continue;

                // Check exclusion
                bool isExcluded = std::any_of(slaacExclusionPrefixes.begin(), slaacExclusionPrefixes.end(), [&](IPAddress p) {
                    return Functions::isSubnetOf(opt.value + 14, prefixLen, p.raw, prefixLen, AddressFamily::IPv6);
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

                    uint8_t mac[6];
                    Functions::calculateEui64(slaacAddr->ip, opt.value + 14, currentInterface->configs.getMac(mac));

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

    void Ndp::receiveRedirectMessage(const Icmpv6Header& redirect, const uint8_t* sourceIp)
    {
        auto trail = redirect.getTrail();
        IPAddress destinationIp;
        IPAddress betterNextHop;
        destinationIp.isV6 = true;
        betterNextHop.isV6 = true;
        std::memcpy(betterNextHop.raw, trail.data(), 16);
        std::memcpy(destinationIp.raw, trail.data() + 16, 16);
        uint64_t nextHopMac;
        bool macFound = false;

        std::vector<TLV8Option> options;
        parseIcmpv6Options(trail.data() + 32, trail.size() - 32, options);

        for (const auto& opt : options)
        {
            if (opt.type == ICMPV6_OPTION_NDP_TARGET && opt.valueSize == 6)
            {
                nextHopMac = readU48(opt.value);
                macFound = true;
                break;
            }
        }

        if (macFound)
        {
            addNdpEntry(betterNextHop, nextHopMac);
        }
    }

    void Ndp::duplicateAddressDetection(InterfaceConfigs::IPv6State::IPv6Address* addr, bool isLinkLocal)
    {
        IPAddress ipCopy;
        ipCopy.isV6 = true;
        std::memcpy(ipCopy.raw, addr->ip, 16);

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
                std::lock_guard<std::mutex> lock(requestMutex);
                if (!pendingDadReschedules.count(ipCopy))
                {
                    pendingDadReschedules.emplace(
                        ipCopy,
                        global.timeManager.addTimer(
                            global.configs.nsfStartTime + suppressWindow,
                            [this, addr, ipCopy, isLinkLocal]() {
                                pendingDadReschedules.erase(ipCopy);
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
            nsRetryCount[ipCopy] = 0;
        }

        // Clear existing entry in the cache (DAD must be clean)
        {
            std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
            ndpCache.erase(ipCopy);
        }

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
        IPAddress ipCopy;
        ipCopy.isV6 = true;
        std::memcpy(ipCopy.raw, addr->ip, 16);
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
            currentInterface->markAddressDuplicate(addr->ip, isLinkLocal);
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
            PacketBuilder ns(currentInterface);
            neighborSolicitation(ns, ipCopy, nullptr);
            uint8_t multicastSolicitation[16];

            {
                IPPacket::BuildIP build = {
                    .iface = currentInterface,
                    .packetInfo = ns,
                    .destIp = generateMulticastSolicitationAddress(multicastSolicitation, addr->ip),
                    .sourceIp = IPV6_SOURCE,
                    .protocolType = IP_ICMPV6
                };

                IPPacket::buildIpv6(build);
            }

            pendingRequests.insert(ipCopy);
            nsRetryCount[ipCopy]++;

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
                uint8_t localAddr[16];
                if (iface.ipv6.getLocalAddress(localAddr))
                    sendRouteAdvertisement(ETHERNET_MAC_BROADCAST, localAddr);

                // Reschedule next RA
                raTimerIds.erase(*raTimerId);
                delete raTimerId;
                scheduleNextRA();
            }
        );

        raTimerIds.insert(*raTimerId);
    }

    void Ndp::scheduleNeighborSolicitation(const IPAddress& targetIp)
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
                            [this, ip = targetIp]() { retryNud(ip); }
                        );

                        currentNudProbes.fetch_sub(1, std::memory_order_seq_cst);

                        IPAddress retryIp;
                        {
                            std::lock_guard<std::mutex> requestLock(requestMutex);
                            if (!queuedNudProbes.empty())
                            {
                                retryIp = std::move(*queuedNudProbes.begin());
                                queuedNudProbes.erase(queuedNudProbes.begin());
                            }
                        }
                        if (retryIp.v6 != 0)
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
            uint8_t mac[6];
            iface.getMac(mac);
            PacketBuilder nsPacket(currentInterface);
            neighborSolicitation(nsPacket, targetIp, mac);
            uint8_t multicastSolicitation[16];

            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = nsPacket,
                .destIp = generateMulticastSolicitationAddress(multicastSolicitation, targetIp.raw),
                .protocolType = IP_ICMPV6
            };

            IPPacket::buildIpv6(build);
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

    void Ndp::retryNud(const IPAddress& targetIp)
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
    
    void Ndp::addSlaacExclusionPrefix(const IPAddress& prefix, bool remove)
    {
        if (remove)
        {
            std::erase_if(slaacExclusionPrefixes, [&](IPAddress pref) { return pref == prefix; });
        }
        else if (!std::any_of(
            slaacExclusionPrefixes.begin(),
            slaacExclusionPrefixes.end(),
            [&](IPAddress pref) { return pref == prefix; })
        )
        {
            slaacExclusionPrefixes.push_back(prefix);
        }
    }
    
    void Ndp::addRaGuardAllowedMac(const uint8_t* mac, bool remove)
    {
        if (remove)
        {
            raGuardAllowedMacs.erase(readU48(mac));
        }
        else
        {
            raGuardAllowedMacs.insert(readU48(mac));
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
