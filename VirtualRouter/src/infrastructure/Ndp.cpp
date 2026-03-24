// Ndp.cpp

// TODO naglean
// TODO nudigp

#include <Global.h>
#include <VirtualRouter.h>

#include "Ndp.h"
#include "IPPacket.h"
#include "processing/PacketBuilder.hpp"
#include "Ethernet.h"
#include "interface/InterfaceManager.h"

namespace infrastructure
{

static uint8_t* calculateEui64(uint8_t* out, const uint8_t* prefix, const uint8_t* mac)
{
    std::memcpy(out, prefix, 8);
    out[8] = mac[0] ^ 0x02;
    out[9]  = mac[1];
    out[10] = mac[2];
    out[11] = 0xFF;
    out[12] = 0xFE;
    out[13] = mac[3];
    out[14] = mac[4];
    out[15] = mac[5];
    return out;
}

// Constructor: Initiates the NDP object with the given interface
Ndp::Ndp(interface::Interface& iface)
    : currentInterface(&iface),
    global(iface.getVRF()->getGlobal())
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

bool Ndp::isShutdown()
{
    return !running.load(std::memory_order_relaxed);
}

void Ndp::addNdpEntry(types::IPv6Address targetIp, uint64_t targetMac, bool proxy, bool isStatic)
{
    if (!isStatic)
    {
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        uint32_t limit = configs.interfaceLimit.load(std::memory_order_relaxed);
        if (limit != 0 && ndpCache.size() >= limit)
        {
            // Remove an entry to enforce a limit.
            types::IPv6Address evicted = insertionOrder.front();
            insertionOrder.erase(insertionOrder.begin());
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
        uint16_t refresh = global.configs.ndp.nudRefreshPeriod.load(std::memory_order_relaxed);
        if (refresh > 0)
        {
            entry.timerId = global.timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::seconds(refresh),
                [this, targetIp](uint32_t) {
                    refreshNeighborEntry(targetIp);
                }
            );
        }
        else
        {
            // Schedule a timer for NUD reachable time.
            entry.timerId = global.timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                [this, targetIp](uint32_t) { onReachableTimeout(targetIp);
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
uint8_t* Ndp::getMac(uint8_t* out, types::IPv6Address ip)
{
    std::shared_lock<std::shared_mutex> lock(ndpCacheMutex);
    {

        std::shared_lock<std::shared_mutex> neighborLock(global.configs.ndp.neighborMutex);
        auto staticIt = staticNdpCache.find(ip);
        if (staticIt != staticNdpCache.end())
        {
            utils::writeU48(out, staticIt->second.macAddress);
            return out;
        }
    }
    auto it = ndpCache.find(ip);
    if (it != ndpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime)
    {
        utils::writeU48(out, it->second.macAddress);
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
    uint64_t mac = iface.getMac();
    processing::PacketBuilder rs(currentInterface);
    routeSolicitation(rs, mac);

    ippacket::BuildIP build = {
        .iface = currentInterface,
        .packetInfo = rs,
        .destIp = ICMPV6_ALL_ROUTERS,
        .protocolType = IP_ICMPV6
    };

    ippacket::buildIpv6(build);
}

void Ndp::resolveAndSend(types::IPv6Address targetIp, processing::PacketBuilder& packetToSend)
{
    {
        std::lock_guard<std::mutex> lock(packetQueueMutex);
        packetQueuePerIp[targetIp].emplace(packetToSend);
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

void Ndp::sendNeighborSolicitation(types::IPv6Address targetIp)
{
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        if (pendingRequests.count(targetIp)) return;
        pendingRequests.insert(targetIp);
        nsRetryCount[targetIp] = 0;
    }
    scheduleNeighborSolicitation(targetIp);
}

void Ndp::receiveNeighborAdvertisement(const packet::Icmpv6Header& receivedNA, types::IPv6Address targetIp)
{
    auto trail = receivedNA.getTrail();
    uint64_t mac;
    bool macFound = false;

    std::vector<packet::TLV8Option> options;
    packet::parseIcmpv6Options(trail.data() + 16, trail.size() - 16, options);

    for (const auto& opt : options)
    {
        // Extract MAC from options
        if (opt.type == ICMPV6_OPTION_NDP_TARGET && opt.valueSize == 6)
        {
            mac = utils::readU48(opt.value);
            macFound = true;
            break;
        }
    }
    if (!macFound) return;

    {
        {
            __uint128_t addrValue = utils::readU128(trail.data());
            std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
            for (const auto& addr : currentInterface->configs.ipv6.globalAddresses)
            {
                if (addr->prefix == addrValue && addr->tentative && targetIp == IPV6_SOURCE)
                {
                    std::lock_guard<std::mutex> lk(neighborReplyStatusMutex);
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
                uint16_t refresh = global.configs.ndp.nudRefreshPeriod.load(std::memory_order_relaxed);
                if (refresh > 0)
                {
                    it->second.timerId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::seconds(refresh),
                        [this, targetIp](uint32_t) {
                            refreshNeighborEntry(targetIp);
                        }
                    );
                }
                else
                {
                    it->second.timerId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                        [this, targetIp](uint32_t) { onReachableTimeout(targetIp); }
                    );
                }
            }
            else
            {
                NdpCacheEntry entry;
                entry.macAddress = mac;
                entry.state = NudState::REACHABLE;
                entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.cacheExpire.load(std::memory_order_relaxed));
                uint16_t refresh = global.configs.ndp.nudRefreshPeriod.load(std::memory_order_relaxed);
                if (refresh > 0)
                {
                    entry.timerId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::seconds(refresh),
                        [this, targetIp](uint32_t) {
                            refreshNeighborEntry(targetIp);
                        }
                    );
                }
                else
                {
                    entry.timerId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::milliseconds(configs.reachableTime.load(std::memory_order_relaxed)),
                        [this, targetIp](uint32_t) { onReachableTimeout(targetIp); }
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

void Ndp::receiveNeighborSolicitation(const packet::Icmpv6Header& nsHeader, types::IPv6Address srcIp, uint64_t srcMac)
{
    auto trail = nsHeader.getTrail();
    types::IPv6Address targetIp = utils::readU128(trail.data());
    uint64_t replyMac;

    bool isOwned = false;
    bool isProxy = false;

    {
        if (currentInterface->configs.ipv6.hasAddress(trail.data()))
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
    processing::PacketBuilder na(currentInterface);
    if (isProxy)
    {
        types::IPv6Address adv = trail.data();
        neighborAdvertisement(na, replyMac, &adv);
    }
    else
    {
        neighborAdvertisement(na, replyMac, &srcIp);
    }

    if (srcMac == 0 && srcIp == IPV6_SOURCE)
    {
        // Multicast NA for DAD response or missing MAC
        ippacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = na,
            .destIp = IPV6_MULTICAST,
            .sourceIp = targetIp,
            .protocolType = IP_ICMPV6
        };

        ippacket::buildIpv6(build);
    }
    else
    {
        // Unicast NA back to sender
        ippacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = na,
            .destIp = srcIp,
            .sourceIp = targetIp,
            .destMac = srcMac,
            .protocolType = IP_ICMPV6
        };

        ippacket::buildIpv6(build);
    }
}

void Ndp::processQueuedPackets(types::IPv6Address targetIp, uint64_t macAddress)
{
    std::queue<processing::PacketBuilder> packets;
    {
        std::lock_guard<std::mutex> lock(packetQueueMutex);
        if (auto que = packetQueuePerIp.extract(targetIp))
        {
            packets = std::move(que.mapped());
        }
    }

    while (!packets.empty())
    {
        processing::PacketBuilder pkt = std::move(packets.front());
        packets.pop();
        auto current = pkt.previewNextBuildHeader();
        if (!current) continue;

        // Continue building next header
        switch (current->type)
        {
            //TODO add more headers
            case packet::HeaderType::ETHERNET:
                ethernet::build(currentInterface, pkt, macAddress, ETHERNET_IPV6);
                break;
            default:
                continue;
        }
    }
}

void Ndp::onReachableTimeout(types::IPv6Address targetIp)
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

void Ndp::startNud(types::IPv6Address targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock)
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

void Ndp::refreshNeighborEntry(types::IPv6Address targetIp)
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

types::IPv6Address Ndp::generateMulticastSolicitationAddress(types::IPv6Address targetIp)
{
    types::IPv6Address out(ICMPV6_SOLICIT_MULTICAST);
    out.raw()[13] = targetIp.raw()[13];
    out.raw()[14] = targetIp.raw()[14];
    out.raw()[15] = targetIp.raw()[15];
    return out;
}

void Ndp::neighborSolicitation(processing::PacketBuilder& packet, types::IPv6Address targetIp, uint64_t* currentMac)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0); // Will set size later

    packet::Icmpv6Header icmp;
    
    processing::BuildEntry* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6)
        return;

    icmp.setBuffer(nextHeader->buffer);

    icmp.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
    icmp.setCode(0);
    icmp.setReservedInt(0);

    uint8_t* trail = icmp.getTrailData();
    utils::writeU128(trail, targetIp.addr);

    if (currentMac)
    {
        packet::TLV8BufferManager options(trail + 16, 8);
        uint8_t* buf = options.getNextValBuf(6);
        utils::writeU48(buf, *currentMac);
        options.append(ICMPV6_OPTION_NDP_TARGET, 1, nullptr, 6);
        nextHeader->length = packet::Icmpv6Header::fixedSize + options.size();
    }

    packet.bufferOffset += nextHeader->length;
}

void Ndp::neighborAdvertisement(processing::PacketBuilder& packet, uint64_t currentMac, types::IPv6Address* targetIp = nullptr)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0); // Will set size later

    packet::Icmpv6Header icmp;

    processing::BuildEntry* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6)
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
        utils::writeU128(trail, targetIp->addr);
    else
        utils::writeU128(trail, currentInterface->configs.ipv6.getLocalAddress().addr);

    packet::TLV8BufferManager options(trail + 16, 8);
    uint8_t* buf = options.getNextValBuf(6);
    utils::writeU48(buf, currentMac);
    options.append(ICMPV6_OPTION_NDP_TARGET, 1, nullptr, 6);
}

void Ndp::routeSolicitation(processing::PacketBuilder& packet, uint64_t currentMac)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0); // Will set size later

    packet::Icmpv6Header icmp;

    processing::BuildEntry* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6)
        return;

    icmp.setBuffer(nextHeader->buffer);

    icmp.setType(ICMPV6_OPCODE_NDP_ROUTE_SOLICITATION);
    icmp.setCode(0);
    icmp.setReservedInt(0);
    
    // Get trail pointer
    uint8_t* trail = icmp.getTrailData();
    
    packet::TLV8BufferManager options(trail, 8);
    uint8_t* buf = options.getNextValBuf(6);
    utils::writeU48(buf, currentMac);
    options.append(ICMPV6_OPTION_NDP_SOURCE, 1, nullptr, 6);

    nextHeader->length = packet::Icmpv6Header::fixedSize + options.size();
    packet.bufferOffset += nextHeader->length;
}

void Ndp::routeAdvertisement(processing::PacketBuilder& packet, uint64_t currentMac)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0); // Will set size later

    packet::Icmpv6Header icmp;

    processing::BuildEntry* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6)
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
    utils::writeU16(reserved + 2, configs.routerLifetime.load(std::memory_order_relaxed));
    icmp.setReserved(reserved);

    uint8_t* trail = icmp.getTrailData();
    utils::writeU32(trail, configs.reachableTime.load(std::memory_order_relaxed));
    utils::writeU32(trail + 4, 0); // 0 means use your own timer

    packet::TLV8BufferManager options(trail + 8);
    uint8_t* buf = options.getNextValBuf(6);
    utils::writeU48(buf, currentMac);
    options.append(ICMPV6_OPTION_NDP_SOURCE, 1, nullptr, 6);

    if (!configs.mtuSuppress.load(std::memory_order_relaxed))
    {
        uint8_t mtu[6];
        utils::writeU16(mtu, 0); // Reserved
        utils::writeU32(mtu + 2, currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed));
        options.append(ICMPV6_OPTION_NDP_MTU, 1, mtu, 6);
    }

    if (configs.autoConfigPrefix.load(std::memory_order_relaxed))
    {
        std::shared_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
        for (const auto& addr : currentInterface->configs.ipv6.globalAddresses)
        {
            if (!addr->valid) continue;

            const uint8_t prefixLen = addr->prefix.prefixLength;
            uint8_t flags = 0;
            flags |= 0x80; // L = on-link
            flags |= 0x40; // A = autonomous

            uint32_t lifetime = configs.raLifetime.load(std::memory_order_relaxed);
            uint32_t preferredLifetime = configs.raPreferredLifetime.load(std::memory_order_relaxed);
            {
                std::shared_lock<std::shared_mutex> lk(configs.configMutex);
                if (configs.raIntervalMS)
                {
                    lifetime /= 1000;
                    preferredLifetime /= 1000;
                }
            }

            uint8_t value[30];
            value[0] = prefixLen;
            value[2] = flags;
            utils::writeU16(value + 2, 0);
            utils::writeU32(value + 4, lifetime);
            utils::writeU32(value + 8, preferredLifetime);
            utils::writeU32(value + 12, 0);
            utils::writeU128(value + 16, types::IPv6Address{addr->prefix.addr, prefixLen}.addr);

            options.append(ICMPV6_OPTION_NDP_PREFIX, 4, value, 30);
        }
    }
    nextHeader->length = packet::Icmpv6Header::fixedSize + options.size();
    packet.bufferOffset += nextHeader->length;
}

void Ndp::sendNeighborAdvertisement(uint64_t destMac, types::IPv6Address targetIp)
{
    if (configs.suppressNA.load(std::memory_order_relaxed))
        return;

    if (currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        return;

    auto& iface = currentInterface->configs;
    processing::PacketBuilder naPacket(currentInterface);

    // Build the ICMPv6 Neighbor Advertisement header
    neighborAdvertisement(naPacket, iface.getMac(), &targetIp);

    // Build the IPv6 packet
    ippacket::BuildIP build = {
        .iface = currentInterface,
        .packetInfo = naPacket,
        .destIp = targetIp,
        .destMac = destMac,
        .protocolType = IP_ICMPV6
    };

    ippacket::buildIpv6(build);
}

void Ndp::sendNeighborAdvertisement()
{
    if (configs.suppressNA.load(std::memory_order_relaxed))
        return;

    types::IPv6Address targetIp = currentInterface->configs.ipv6.getLocalAddress();

    // Rate-limit unsolicited NAs per advertised IP
    {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(requestMutex);
        auto& lastTime = lastUnsolicitedNaTime[targetIp];
        if (now - lastTime < std::chrono::seconds(1))
            return;
        lastTime = now;
    }

    if (currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        return;

    auto& iface = currentInterface->configs;
    processing::PacketBuilder naPacket(currentInterface);

    // Build the ICMPv6 Neighbor Advertisement header
    neighborAdvertisement(naPacket, iface.getMac());

    // Destination is solicited-node multicast corresponding to the advertised IP
    types::IPv6Address solicitedNodeMulticast = generateMulticastSolicitationAddress(targetIp);

    // Build the IPv6 packet
    ippacket::BuildIP build = {
        .iface = currentInterface,
        .packetInfo = naPacket,
        .destIp = solicitedNodeMulticast,
        .protocolType = IP_ICMPV6
    };

    ippacket::buildIpv6(build);
}

void Ndp::sendRouteSolicitation(types::IPv6Address targetIp)
{
    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        auto& iface = currentInterface->configs;
        processing::PacketBuilder rsPacket(currentInterface);
        routeSolicitation(rsPacket, iface.getMac());

        // Set the IP header and send the packet.
        ippacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = rsPacket,
            .destIp = generateMulticastSolicitationAddress(targetIp),
            .protocolType = IP_ICMPV6
        };

        ippacket::buildIpv6(build);
    }
}

void Ndp::sendRouteAdvertisement(uint64_t targetMac, types::IPv6Address targetIp)
{
    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        auto& iface = currentInterface->configs;
        // Gather interface values.
        processing::PacketBuilder raPacket(currentInterface);
        routeAdvertisement(raPacket, iface.getMac());
        
        // Set the IP header and send the packet.
        ippacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = raPacket,
            .destIp = targetIp,
            .destMac = targetMac,
            .protocolType = IP_ICMPV6
        };

        ippacket::buildIpv6(build);
    }
}

void Ndp::sendRedirectMessage(types::IPv6Address targetIp, types::IPv6Address destinationIp)
{
    if (!currentInterface || !currentInterface->getVRF() || !configs.redirects.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder packet(currentInterface);

    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0); // Will set size later

    packet::Icmpv6Header icmp;

    processing::BuildEntry* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6)
        return;
    
    icmp.setBuffer(nextHeader->buffer);

    icmp.setType(ICMPV6_OPCODE_NDP_REDIRECT_MESSAGE);
    icmp.setCode(0);
    icmp.setReserved(0);
    
    uint8_t* trail = icmp.getTrailData();
    
    utils::writeU128(trail, destinationIp.addr);
    utils::writeU128(trail + 16, targetIp.addr);
    
    packet::TLV8BufferManager options(trail + 32, 8);
    options.append(ICMPV6_OPTION_NDP_TARGET, 1, 0, 0);
    currentInterface->configs.getMac(trail + 34);

    nextHeader->length = packet::Icmpv6Header::fixedSize + 32 + options.size();
    packet.bufferOffset += nextHeader->length;

    ippacket::BuildIP build = {
        .iface = currentInterface,
        .packetInfo = packet,
        .destIp = destinationIp,
        .protocolType = IP_ICMPV6
    };

    ippacket::buildIpv6(build);
}

void Ndp::sendRedirectIfNeeded(const packet::PacketInfo& originalPacket, const uint8_t* pkt)
{
    //TODO move to packet forwarder
    if (!currentInterface || currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        return;

    // Ensure original packet is IPv6
    packet::IPv6HeaderRaw* ipv6 = nullptr;
    auto headers = originalPacket.headers;
    for (size_t i = 0; i < originalPacket.count; ++i)
    {
        auto header = headers[i];
        if (header.type == packet::HeaderType::IPV6)
        {
            ipv6 = reinterpret_cast<packet::IPv6HeaderRaw*>(const_cast<uint8_t*>(pkt) + header.offset);
        }
    }

    if (!ipv6) return;
/*
    // Destination must not be multicast
    if (ipHeader.destinationAddress.isMulticast() || ipHeader.sourceAddress.isMulticast())
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

void Ndp::receiveRouteAdvertisement(const packet::Icmpv6Header& receivedRA, types::IPv6Address sourceIp, uint64_t sourceMac)
{
    if (configs.suppressRA.load(std::memory_order_relaxed)) return;

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
    uint8_t flags = receivedRA.getReserved()[1];
    routerLifetime = utils::readU16(receivedRA.getReserved() + 2);

    mFlag = flags & 0x80;
    oFlag = flags & 0x40;

    uint8_t prfBits = (flags >> 3) & 0b11;

    if (configs.autoConfigDefaultRoute.load(std::memory_order_relaxed) && routerLifetime > 0 && sourceIp == IPV6_SOURCE)
    {
        if (global.configs.ndp.ndAsRouteOwner.load(std::memory_order_relaxed))
        {
            //TODO make ndp interface owner
        }
        //TODO add default route
    }

    std::vector<packet::TLV8Option> options;
    auto trail = receivedRA.getTrail();
    packet::parseIcmpv6Options(trail.data() + 8, trail.size() - 8, options);

    // Process each RA option (only prefix and mtu)
    for (const auto& opt : options)
    {
        if (opt.type == ICMPV6_OPTION_NDP_PREFIX && opt.valueSize >= 30)
        {
            uint8_t prefixLen = opt.value[0];
            uint8_t prefixFlags = opt.value[1];
            bool A = prefixFlags & 0x40;

            uint32_t validLifetime = utils::readU32(opt.value + 2);
            uint32_t preferredLifetime = utils::readU32(opt.value + 6);

            if (preferredLifetime > validLifetime) continue;

            // Check exclusion
            bool isExcluded = std::any_of(slaacExclusionPrefixes.begin(), slaacExclusionPrefixes.end(), [&](types::IPv6Address p) {
                return p.contains(opt.value + 14, prefixLen);
            });
            if (isExcluded) continue;

            // SLAAC (only if explicitly enabled by config)
            if (A && validLifetime > 0 && preferredLifetime <= validLifetime && configs.slaacEnabled.load(std::memory_order_relaxed))
            {
                interface::InterfaceConfigs::IPv6State::IPv6Address* slaacAddr = new interface::InterfaceConfigs::IPv6State::IPv6Address();
                slaacAddr->prefix.prefixLength = prefixLen;
                slaacAddr->tentative = true;
                slaacAddr->valid = false;
                slaacAddr->globalTentative = true;
                slaacAddr->globalValid = false;
                slaacAddr->deprecated = false;
                slaacAddr->preferredLifetime = preferredLifetime;

                uint8_t mac[6];
                uint8_t slac[16];
                // TODO: fix this
                calculateEui64(slac, opt.value + 14, currentInterface->configs.getMac(mac));
                slaacAddr->prefix.addr = utils::readU128(slac);

                {
                    std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                    currentInterface->configs.ipv6.globalAddresses.push_back(slaacAddr);
                }

                slaacAddr->expirationId = global.timeManager.addTimer(
                    std::chrono::steady_clock::now() + std::chrono::seconds(validLifetime),
                    [this, slaacAddr](uint32_t)
                    {
                        std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                        slaacAddr->globalValid = false;
                        slaacAddr->expirationId = 0;
                    }
                );

                slaacAddr->preferedExpirationId = global.timeManager.addTimer(
                    std::chrono::steady_clock::now() + std::chrono::seconds(preferredLifetime),
                    [this, slaacAddr](uint32_t)
                    {
                        std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
                        slaacAddr->deprecated = true;
                        slaacAddr->preferedExpirationId = 0;
                    }
                );

                duplicateAddressDetection(*slaacAddr);
            }
        }
    }
}

void Ndp::receiveRedirectMessage(const packet::Icmpv6Header& redirect, types::IPv6Address sourceIp)
{
    auto trail = redirect.getTrail();
    types::IPv6Address betterNextHop = utils::readU128(trail.data());
    types::IPv6Address destinationIp = utils::readU128(trail.data() + 16);
    uint64_t nextHopMac;
    bool macFound = false;

    std::vector<packet::TLV8Option> options;
    packet::parseIcmpv6Options(trail.data() + 32, trail.size() - 32, options);

    for (const auto& opt : options)
    {
        if (opt.type == ICMPV6_OPTION_NDP_TARGET && opt.valueSize == 6)
        {
            nextHopMac = utils::readU48(opt.value);
            macFound = true;
            break;
        }
    }

    if (macFound)
    {
        addNdpEntry(betterNextHop, nextHopMac);
    }
}

void Ndp::duplicateAddressDetection(interface::InterfaceConfigs::IPv6State::IPv6Address& addr)
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
            std::lock_guard<std::mutex> lock(requestMutex);
            if (!pendingDadReschedules.count(addr.prefix.addr))
            {
                pendingDadReschedules.emplace(
                    addr.prefix.addr,
                    global.timeManager.addTimer(
                        global.configs.nsfStartTime + suppressWindow,
                        [this, &addr, ip = addr.prefix.addr](uint32_t) {
                            pendingDadReschedules.erase(ip);
                            duplicateAddressDetection(addr);
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
    if (!addr.tentative)
        return;

    {
        std::lock_guard<std::mutex> lock(requestMutex);
        nsRetryCount[addr.prefix.addr] = 0;
    }

    // Clear existing entry in the cache (DAD must be clean)
    {
        std::unique_lock<std::shared_mutex> lock(ndpCacheMutex);
        ndpCache.erase(addr.prefix.addr);
    }

    // Register traching for replies
    {
        std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
        neighborReplyStatus[addr.prefix.addr] = false;
    }

    // Start DAD
    preformDad(addr);
}

void Ndp::preformDad(interface::InterfaceConfigs::IPv6State::IPv6Address& addr)
{
    // Local capture values
    const int maxAttempts = configs.dadAttempts.load(std::memory_order_relaxed);
    const auto delay = std::chrono::milliseconds(configs.dadTime.load(std::memory_order_relaxed));

    int attempt = nsRetryCount[addr.prefix.addr];
    // Check if reply was received
    bool isDuplicate = false;
    {
        std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
        if (neighborReplyStatus.count(addr.prefix.addr))
        {
            isDuplicate = neighborReplyStatus[addr.prefix.addr];
        }
    }

    bool remove = false;
    if (isDuplicate)
    {
        {
            std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
            addr.tentative = false;
            addr.valid = false;
        }
        currentInterface->markAddressDuplicate(addr.prefix);
        remove = true;
    }
    else if (attempt >= maxAttempts)
    {
        std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
        addr.tentative = false;
        addr.valid = true;
        currentInterface->setIPv6Ready(addr.prefix);
        remove = true;
    }
    else
    {
        // Send anonymous NS (source = ::, no MAC option)
        processing::PacketBuilder ns(currentInterface);
        neighborSolicitation(ns, addr.prefix, nullptr);
        {
            ippacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = ns,
                .destIp = generateMulticastSolicitationAddress(addr.prefix),
                .sourceIp = IPV6_SOURCE,
                .protocolType = IP_ICMPV6
            };

            ippacket::buildIpv6(build);
        }

        pendingRequests.insert(addr.prefix);
        nsRetryCount[addr.prefix]++;

        uint32_t timerId = global.timeManager.addTimer(
            std::chrono::steady_clock::now() + delay,
            [this, &addr](uint32_t) {
                preformDad(addr);
            }
        );
        {
            std::lock_guard<std::mutex> lock(requestMutex); // reuse existing mutex
            dadTimers[addr.prefix] = timerId;
        }

    }

    if (remove)
    {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(addr.prefix);
            nsRetryCount.erase(addr.prefix);
            dadTimers.erase(addr.prefix);
        }

        {
            std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
            neighborReplyStatus.erase(addr.prefix);
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
    
    uint32_t baseInterval;
    {
        std::shared_lock<std::shared_mutex> lock(configs.configMutex);
        baseInterval = configs.raInterval;
        if (configs.advertisementInterval)
        {
            uint32_t min = configs.raIntervalMin;
            if (min > baseInterval) min = baseInterval;
            uint32_t delta = baseInterval - min;
            baseInterval = min + (static_cast<uint32_t>(rand()) % (delta + 1));
        }
    }
    uint32_t raTimerId = global.timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(baseInterval),
        [this](uint32_t timerId)
        {
            if (!running.load(std::memory_order_relaxed) ||
                configs.suppressRA.load(std::memory_order_relaxed))
                return;
            
            auto& iface = currentInterface->configs;
            auto localAddr = iface.ipv6.getLocalAddress();
            if (localAddr.addr)
                sendRouteAdvertisement(utils::readU48(ETHERNET_MAC_BROADCAST), localAddr);

            // Reschedule next RA
            raTimerIds.erase(timerId);
            scheduleNextRA();
        }
    );

    raTimerIds.insert(raTimerId);
}

void Ndp::scheduleNeighborSolicitation(types::IPv6Address targetIp)
{
    if (!running.load(std::memory_order_relaxed)) return;

    bool runningNud = false; // Is NUD running

    uint8_t attempt = nsRetryCount[targetIp];
    uint16_t maxAttempts = 3; // Default max retries for NS

    NdpCacheEntry* entry = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(configs.configMutex); // Lock for nud values, this makes all nud values thread safe at the same time for reconfiguration.
        {
            std::unique_lock<std::shared_mutex> lk(ndpCacheMutex);
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
                std::unique_lock<std::shared_mutex> lk(ndpCacheMutex);
                if (runningNud && entry && entry->nudGroup <= configs.nudBase)
                {
                    entry->state = NudState::UNREACHABLE;
                    uint32_t finalWait = configs.nudFinalWait;
                    entry->nudRetryTimerId = global.timeManager.addTimer(
                        std::chrono::steady_clock::now() + std::chrono::milliseconds(finalWait),
                        [this, ip = targetIp](uint32_t) { retryNud(ip); }
                    );

                    currentNudProbes.fetch_sub(1, std::memory_order_seq_cst);

                    types::IPv6Address retryIp;
                    {
                        std::lock_guard<std::mutex> requestLock(requestMutex);
                        if (!queuedNudProbes.empty())
                        {
                            retryIp = std::move(*queuedNudProbes.begin());
                            queuedNudProbes.erase(queuedNudProbes.begin());
                        }
                    }
                    if (retryIp.addr != 0)
                    {
                        auto it = ndpCache.find(retryIp);
                        if (it != ndpCache.end())
                        {
                            startNud(retryIp, it->second, lk);
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
                std::lock_guard<std::mutex> lk(requestMutex);
                pendingRequests.erase(targetIp);
                nsRetryCount.erase(targetIp);
                nsRetryTimers.erase(targetIp);
            }

            {
                std::lock_guard<std::mutex> lk(neighborReplyStatusMutex);
                neighborReplyStatus.erase(targetIp);
            }

            return;
        }
    }

    // Send the solicitation
    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        auto& iface = currentInterface->configs;
        uint64_t mac = iface.getMac();
        processing::PacketBuilder nsPacket(currentInterface);
        neighborSolicitation(nsPacket, targetIp, &mac);

        ippacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = nsPacket,
            .destIp = generateMulticastSolicitationAddress(targetIp),
            .protocolType = IP_ICMPV6
        };

        ippacket::buildIpv6(build);
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
        [this, targetIp](uint32_t)
        {
            {
                std::lock_guard<std::mutex> lock(neighborReplyStatusMutex);
                if (neighborReplyStatus.count(targetIp) && neighborReplyStatus[targetIp])
                {
                    std::lock_guard<std::mutex> lk(requestMutex);
                    pendingRequests.erase(targetIp);
                    nsRetryCount.erase(targetIp);
                    return;
                }
            }
            scheduleNeighborSolicitation(targetIp);
        }
    );
}

void Ndp::retryNud(types::IPv6Address targetIp)
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
            std::lock_guard<std::mutex> lk(requestMutex);
            nsRetryCount[targetIp] = 0;
            pendingRequests.insert(targetIp);
        }
        it->second.nudGroup++;
    }

    // Retry NUD: send NS
    sendNeighborSolicitation(targetIp);
}

void Ndp::addSlaacExclusionPrefix(types::IPv6Address prefix, bool remove)
{
    if (remove)
    {
        std::erase_if(slaacExclusionPrefixes, [&](types::IPAddress pref) { return pref == prefix; });
    }
    else if (!std::any_of(
        slaacExclusionPrefixes.begin(),
        slaacExclusionPrefixes.end(),
        [&](types::IPAddress pref) { return pref == prefix; })
    )
    {
        slaacExclusionPrefixes.push_back(prefix);
    }
}

void Ndp::addRaGuardAllowedMac(uint64_t mac, bool remove)
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
} // namespace infrastructure
