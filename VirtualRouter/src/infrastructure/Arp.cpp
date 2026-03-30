// Arp.h

// TODO: remove ethernet from arp, put it into queue

#include <Global.h>
#include <VirtualRouter.h>
#include <ByteUtils.hpp>

#include "Arp.h"
#include "interface/Interface.h"
#include "packet/headers/ArpHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "configs/registry/global/GlobalRegistry.h"
#include "Ethernet.h"

// TODO: Have the ability to insert entries when shutdown

namespace infrastructure
{
// Constructor: Initiates the ARP object with the given interface
Arp::Arp(interface::Interface& CurrentInterface) 
    : currentInterface(&CurrentInterface),
    global(CurrentInterface.getVRF()->getGlobal())
{
    if (global.routingEnabled)
        initiateArp();
}

void Arp::initiateArp()
{
    /*
    auto it = global.configs.arp.neighbors.find(currentInterface->getVRF()->getName());
    if (it != global.configs.arp.neighbors.end())
    {
        for (const auto& [ip, neighbor] : it->second)
        {
            if (neighbor.interface == currentInterface->configs.key)
            {
                addArpEntry(ip, neighbor.mac, neighbor.proxy, true);
            }
        }
    }
    */
    running.store(true, std::memory_order_relaxed);
}

// Destructor
Arp::~Arp()
{
    shutdown();
}

// Shutdown method
void Arp::shutdown()
{
    running.store(false, std::memory_order_release);
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        for (auto& [ip, cache] : arpCache)
        {
            if (cache.timerId != 0)
            {
                global.timeManager.cancelTimer(cache.timerId);
            }
        }
        arpCache.clear();
    }

    // Clear resources
    {
        std::lock_guard<std::mutex> lock(replyStatusMutex);
        replyStatus.clear();
    }
    {
        std::lock_guard<std::mutex> lock(packetQueueMutex);
        packetQueuePerIp.clear();
    }
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        pendingRequests.clear();
    }

    incompletes.store(0, std::memory_order_release);
    pendingIncompletes.clear();
}

bool Arp::isShutdown()
{
    return !running.load(std::memory_order_relaxed);
}

void Arp::addArpEntry(types::IPv4Address targetIp, types::Mac targetMac, bool proxy, bool isStatic)
{
    auto now = std::chrono::steady_clock::now();

    if (isStatic)
    {
        ArpCacheEntry entry;
        utils::writeU48(entry.macAddress, targetMac);
        std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
        staticArpCache[targetIp] = entry;

        if (proxy)
        {
            proxyEntries[targetIp] = targetMac;
        }
        return;
    }

    // STICKY ARP: if enabled, do now overwrite existing dynamic entry
    if (global.configs->get<config::Global::IPV4_STICKY_ARP>().load())
    {
        std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
        auto existing = arpCache.find(targetIp);
        if (existing != arpCache.end())
        {
            return;
        }
    }

    // Cancel old timer if already present
    if (arpCache.count(targetIp))
    {
        global.timeManager.cancelTimer(arpCache[targetIp].timerId);
    }

    // Build the entry
    ArpCacheEntry entry;
    utils::writeU48(entry.macAddress, targetMac);
    entry.expiryTime = now + std::chrono::seconds(configs.timeout);

    // Refresh vs expire logic
    entry.timerId = global.timeManager.addTimer(
        entry.expiryTime,
        [this, targetIp](uint32_t) { expireArpEntry(targetIp); }
    );

    // Insert into cache
    {
        std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
        arpCache[targetIp] = entry;
        insertionOrder.push_back(targetIp);

        if (proxy)
        {
            proxyEntries[targetIp] = targetMac;
        }
    }
}

void Arp::removeArpEntry(types::IPv4Address ip, bool isStatic)
{
    {
        std::unique_lock<std::shared_mutex> lock(arpCacheMutex);

        if (isStatic)
        {
            staticArpCache.erase(ip);
        }
        else
        {
            if (arpCache.count(ip))
            {
                if (arpCache[ip].timerId != 0)
                {
                    global.timeManager.cancelTimer(arpCache[ip].timerId);
                }
                arpCache.erase(ip);
            }
        }

        staticArpCache.erase(ip);
        proxyEntries.erase(ip);
        std::erase(insertionOrder, ip);
    }

    if (!isStatic)
    {
        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            replyStatus.erase(ip);
        }

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(ip);
        }

        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp.erase(ip);
        }
    }
}

// Get MAC address for the given ip
bool Arp::getMac(uint8_t* out, types::IPv4Address targetIp)
{
    /*
    std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
    {
        auto staticIt = staticArpCache.find(targetIp);
        if (staticIt != staticArpCache.end())
        {
            std::memcpy(out, staticIt->second.macAddress, 6);
            return true;
        }
    }
    */
    auto it = arpCache.find(targetIp);
    if (it != arpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime && it->second.status == ArpCacheStatus::COMPLETE)
    {
        std::memcpy(out, it->second.macAddress, 6);
        return true;
    }
    return false;
}

// Enqueue a packet for ARP resolution and send once resolved
void Arp::resolveAndSend(types::IPv4Address targetIp, processing::PacketBuilder& packetToSend)
{
    if (!global.configs->get<config::Global::IPV4_ARP_INCOMPLETE>().load())
        return;

    {
        std::lock_guard<std::mutex> lock(packetQueueMutex);
        auto& queue = packetQueuePerIp[targetIp];

        // Enforce queue size limit from global config
        if (queue.size() < global.configs->get<config::Global::IPV4_ARP_QUEUE>().load())
        {
            packetQueuePerIp[targetIp].push(std::move(packetToSend));
        }
    }
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        auto it = arpCache.find(targetIp);
        if (it != arpCache.end()) return;

        // Entry limit enforcement
        auto& incompleteEntries = global.configs->get<config::Global::IPV4_ARP_INCOMPLETE_ENTRIES>();
        if (incompleteEntries.hasValue() && incompletes.load(std::memory_order_relaxed) >= incompleteEntries.load())
        {
            pendingIncompletes.insert(targetIp);
            return; // Too many incomplete entries
        }

        // Add incomplete arp entry
        ArpCacheEntry entry;
        entry.status = ArpCacheStatus::INCOMPLETE;
        arpCache[targetIp] = entry;
        incompletes.fetch_add(1, std::memory_order_seq_cst);
    }

    sendRequest(targetIp);
}

void Arp::expireArpEntry(types::IPv4Address ip)
{
    if (global.configs->get<config::Global::IPV4_ARP_INCOMPLETE>().load())
    {
        auto it = arpCache.find(ip);
        if (it != arpCache.end())
        {
            it->second.timerId = 0;

            if (it->second.status == ArpCacheStatus::INCOMPLETE)
            {
                {
                    std::lock_guard<std::mutex> lock(replyStatusMutex);
                    replyStatus.erase(ip);
                }

                {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    pendingRequests.erase(ip);
                }
                if (incompletes.load(std::memory_order_relaxed) != 0)
                {
                    incompletes.fetch_sub(1, std::memory_order_seq_cst);
                }
            }

            it->second.status = ArpCacheStatus::STALE;

            sendRequest(ip);
        }
    }
    else
    {
        removeArpEntry(ip);
    }
}

// Send an ARP request for the given IP
void Arp::sendRequest(types::IPv4Address targetIp)
{
    {
        // Set request as pending
        std::lock_guard<std::mutex> lock(requestMutex);
        if (pendingRequests.count(targetIp)) return;
        pendingRequests.insert(targetIp);
    }

    {
        // Create reply status entry if not already present
        std::lock_guard<std::mutex> lock(replyStatusMutex);
        if (replyStatus.find(targetIp) == replyStatus.end())
        {
            replyStatus[targetIp].store(false, std::memory_order_release);
        }
    }

    // Find existing incomplete entry
    ArpCacheEntry* entry = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        auto it = arpCache.find(targetIp);
        if (it != arpCache.end())
        {
            entry = &it->second;
        }
    }

    // Send request if incomplete entry exists
    if (entry && entry->status != ArpCacheStatus::COMPLETE)
    {
        scheduleRequest(targetIp, *entry);
    }
}

// Method to receive ARP reply
void Arp::receiveReply(const packet::ArpHeader& receivedReply)
{
    const uint8_t* mac = receivedReply.getSenderHwAddr();
    types::IPv4Address senderIp = receivedReply.getSenderIpAddr();
    types::IPv4Address targetIp = receivedReply.getTargetIpAddr();

    // Ignore gratuitous ARP if disabled
    bool garp = senderIp == targetIp;
    if (garp && (!global.configs->get<config::Global::IPV4_ARP_GRATUITOUS>().load() ||
        !running.load(std::memory_order_relaxed) || !global.routingEnabled ||
        std::memcmp(mac, ETHERNET_MAC_BROADCAST, 6) == 0))
        return;

    if (arpCache.count(senderIp))
    {
        if (arpCache[senderIp].status == ArpCacheStatus::COMPLETE && global.configs->get<config::Global::IPV4_STICKY_ARP>().load())
        {
            // Ignore if sticky ARP is enabled
            return;
        }

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(senderIp);
        }
        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            replyStatus.erase(senderIp);
        }

        {
            std::unique_lock<std::shared_mutex> lock(arpCacheMutex);

            ArpCacheEntry& entry = arpCache[senderIp];
            entry.status = ArpCacheStatus::COMPLETE;
            std::memcpy(entry.macAddress, mac, 6);
            entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.timeout);

            if (entry.timerId != 0)
            {
                lock.unlock();
                global.timeManager.cancelTimer(entry.timerId);
                lock.lock();
            }

            // Expire time
            entry.timerId = global.timeManager.addTimer(
                entry.expiryTime,
                [this, senderIp](uint32_t)
                {
                    expireArpEntry(senderIp);
                }
            );
        }

        if (incompletes.load(std::memory_order_relaxed) != 0)
        {
            incompletes.fetch_sub(1, std::memory_order_seq_cst);
            if (!pendingIncompletes.empty())
            {
                auto pendingIp = *pendingIncompletes.begin();
                pendingIncompletes.erase(pendingIp);
                // Add incomplete arp entry
                {
                    std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
                    ArpCacheEntry entry;
                    entry.status = ArpCacheStatus::INCOMPLETE;
                    arpCache[pendingIp] = entry;
                    insertionOrder.push_back(pendingIp);
                }
                incompletes.fetch_add(1, std::memory_order_seq_cst);
                sendRequest(pendingIp);
            }
        }
    }
    else if (garp)
    {
        std::unique_lock<std::shared_mutex> lock(arpCacheMutex);

        ArpCacheEntry entry;
        entry.status = ArpCacheStatus::COMPLETE;
        std::memcpy(entry.macAddress, mac, 6);
        entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.timeout);
        arpCache[senderIp] = entry;
        insertionOrder.push_back(senderIp);

        // Expire time
        entry.timerId = global.timeManager.addTimer(
            entry.expiryTime,
            [this, senderIp](uint32_t)
            {
                expireArpEntry(senderIp);
            }
        );
    }
    else return;

    {
        std::lock_guard<std::mutex> lock(replyStatusMutex);
        if (replyStatus.count(senderIp))
        {
            replyStatus[senderIp].store(true, std::memory_order_release);
        }
    }

    processQueuedPackets(senderIp, utils::readU64(mac));
}

void Arp::receiveRequest(const packet::ArpHeader& request, types::Mac sourceMac)
{
    types::IPv4Address targetIp = request.raw->targetIpAddress;
    types::IPv4Address senderIp = request.raw->senderIpAddress;

    if (currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;

    if (configs.authorized.load(std::memory_order_relaxed) && !staticArpCache.count(senderIp))
    {
        return;
    }

    // Drop invalid request (e.g., 0.0.0.0 or identical source/target)
    if (targetIp.addr == 0 || targetIp == senderIp)
        return;

    uint64_t replyMac = 0;
    bool isLocal = false;
    bool isProxy = false;

    {
        if (currentInterface->configs.ipv4.comparePrimaryAddress(request.raw->targetIpAddress))
        {
            replyMac = currentInterface->configs.getMac();
            isLocal = true;
        }
        else if (global.configs->get<config::Global::IPV4_ARP_PROXY>().load())
        {
            std::shared_lock<std::shared_mutex> arpLock(arpCacheMutex);
            auto it = proxyEntries.find(targetIp);
            if (it != proxyEntries.end())
            {
                replyMac = it->second;
                isProxy = true;
            }
        }
    }

    if (!isLocal && !isProxy)
        return; // Not for us

    processing::PacketBuilder reply(currentInterface);
    arpReply(reply, replyMac, sourceMac, request.getTargetIpAddr(), request.getSenderIpAddr());
    currentInterface->enqueuePacket(reply, sourceMac);
}

void Arp::processQueuedPackets(types::IPv4Address targetIp, types::Mac macAddress)
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
        auto current = pkt.currentBuildHeader();
        if (!current) continue;

        // Continue building next header
        switch (current->next)
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

void Arp::scheduleRequest(types::IPv4Address targetIp, ArpCacheEntry& entry)
{
    if (!running.load(std::memory_order_relaxed) || !global.routingEnabled) return;

    uint32_t interval;
    uint32_t retries;
    if (entry.status == ArpCacheStatus::INCOMPLETE)
    {
        // TODO calculate the dam interval
        interval = 5; // not the right value, i cant find shit telling me how to calculate this
        retries = global.configs->get<config::Global::IPV4_ARP_INCOMPLETE_RETRY>().load();
    }
    else
    {
        interval = configs.probeInterval.load(std::memory_order_relaxed);
        retries = configs.probeInterval.load(std::memory_order_relaxed);
    }
    
    // Increment retries
    entry.retries++;

    // Initialize reply static for the target IP if not already present
    if (!replyStatus.count(targetIp))
    {
        replyStatus[targetIp].store(false, std::memory_order_release);
    }
    // Check for a response
    else if (replyStatus[targetIp].load(std::memory_order_relaxed))
    {
        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            replyStatus.erase(targetIp);
        }
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(targetIp);
        }
        return;
    }

    // Check if there have been too many retries
    if (entry.retries > retries)
    {
        removeArpEntry(targetIp);

        return; // To many retries, dropping
    }
    std::unique_lock<std::mutex> lock(replyStatusMutex);

    // Send the ARP request
    processing::PacketBuilder arpReq(currentInterface);
    auto& iface = currentInterface->configs;

    arpRequest(arpReq, iface.getMac(), iface.ipv4.getPrimaryAddress(), targetIp);

    //ethernet::build(currentInterface, arpReq, nullptr, &packet::variable::Mac::broadcast, packet::variable::Ethernet::arp);
    currentInterface->enqueuePacket(arpReq, utils::readU48(ETHERNET_MAC_BROADCAST));

    // Wait for the conditional variable to be modified or timeout
    uint32_t timerId = global.timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(interval),
        [this, targetIp, &entry](uint32_t) {
            scheduleRequest(targetIp, entry);
        }
    );
    {
        std::shared_lock<std::shared_mutex> lk(arpCacheMutex);
        entry.timerId = timerId;
    }
}

// Method to send an ARP reply
void Arp::sendReply(types::Mac targetMac, types::IPv4Address targetIp) 
{
    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        auto& interfaceInfo = currentInterface->configs;
        processing::PacketBuilder replyPacket(currentInterface);

        arpReply(replyPacket, interfaceInfo.getMac(), targetMac, interfaceInfo.ipv4.getPrimaryAddress(), targetIp);

        //ethernet::build(currentInterface, replyPacket, nullptr, &targetMac, packet::variable::Ethernet::arp);
        currentInterface->enqueuePacket(replyPacket, targetMac);
    }
}

// Creates an ARP request packet
void Arp::arpRequest(processing::PacketBuilder& packet, types::Mac currentMac, types::IPv4Address sourceIp, types::IPv4Address targetIp) 
{
    packet::EthernetHeader eth;
    packet::ArpHeader arp;

    auto* ethEntry = packet.reserveHeader(packet::HeaderType::ETHERNET, packet::EthernetHeader::fixedSize);
    auto* arpEntry = packet.reserveHeader(packet::HeaderType::ARP, packet::ArpHeader::fixedSize);

    eth.setBuffer(ethEntry->buffer);
    arp.setBuffer(arpEntry->buffer);

    // Set up the Ethernet header for the ARP request
    std::memcpy(eth.raw->destinationMac, ETHERNET_MAC_BROADCAST, 6);
    eth.setSourceMac(currentMac);
    eth.setType(ETHERNET_ARP);

    arp.setHardwareType(ARP_HARDWARE_ETHERNET);
    arp.setProtocolType(ETHERNET_IPV4);
    arp.setHardwareSize(0x06);
    arp.setProtocolSize(0x04);
    arp.setOpcode(ARP_OPCODE_REQUEST);
    arp.setSenderHwAddr(currentMac);
    arp.setSenderIpAddr(sourceIp.addr);
    std::memcpy(arp.raw->targetHardwareAddress, ETHERNET_MAC_BROADCAST, 6);
    arp.setTargetIpAddr(targetIp.addr);
}

// Creates an ARP reply packet
void Arp::arpReply(processing::PacketBuilder& packet, types::Mac currentMac, types::Mac targetMac, types::IPv4Address sourceIp, types::IPv4Address targetIp) 
{
    packet::EthernetHeader eth;
    packet::ArpHeader arp;

    auto* ethEntry = packet.reserveHeader(packet::HeaderType::ETHERNET, packet::EthernetHeader::fixedSize);
    auto* arpEntry = packet.reserveHeader(packet::HeaderType::ARP, packet::ArpHeader::fixedSize);

    eth.setBuffer(ethEntry->buffer);
    arp.setBuffer(arpEntry->buffer);

    // Set up the Ethernet header for the ARP request
    eth.setDestinationMac(targetMac);
    eth.setSourceMac(currentMac);
    eth.setType(ETHERNET_ARP);

    // Set up the ARP header for the reply
    arp.setHardwareType(ARP_HARDWARE_ETHERNET);
    arp.setProtocolType(ETHERNET_IPV4);
    arp.setHardwareSize(0x06);
    arp.setProtocolSize(0x04);
    arp.setOpcode(ARP_OPCODE_REPLY);
    arp.setSenderHwAddr(currentMac);
    arp.setSenderIpAddr(sourceIp.addr);
    arp.setTargetHwAddr(targetMac);
    arp.setTargetIpAddr(targetIp.addr);
}
} // namespace infrastructure
