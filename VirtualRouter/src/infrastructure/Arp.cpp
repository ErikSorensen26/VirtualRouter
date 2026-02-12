// Arp.h

// TODO: remove ethernet from arp, put it into queue

#include <Global.h>
#include <VirtualRouter.h>

#include "Arp.h"
#include "interface/Interface.h"
#include "packet/headers/ArpHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "Ethernet.h"

namespace Protocol 
{

    // Constructor: Initiates the ARP object with the given interface
    Arp::Arp(Interface& CurrentInterface) 
        : currentInterface(&CurrentInterface),
        global(CurrentInterface.getVRF()->getGlobal())
    {
        if (global.routingEnabled)
            initiateArp();
    }

    void Arp::initiateArp()
    {
        std::shared_lock<std::shared_mutex> lock(global.configs.arp.neighborMutex);
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

    void Arp::addArpEntry(uint32_t targetIp, uint64_t targetMac, bool proxy, bool isStatic)
    {
        auto now = std::chrono::steady_clock::now();

        if (isStatic)
        {
            ArpCacheEntry entry;
            writeU48(entry.macAddress, targetMac);
            std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
            staticArpCache[targetIp] = entry;

            if (proxy)
            {
                proxyEntries[targetIp] = targetMac;
            }
            return;
        }

        // STICKY ARP: if enabled, do now overwrite existing dynamic entry
        if (global.configs.arp.stickyArp.load(std::memory_order_relaxed))
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
        writeU48(entry.macAddress, targetMac);
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

    void Arp::removeArpEntry(uint32_t ip, bool isStatic)
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
    bool Arp::getMac(uint8_t* out, const uint8_t* ip)
    {
        uint32_t targetIp = readU32(ip);
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        {
            std::shared_lock<std::shared_mutex> neighborLock(global.configs.arp.neighborMutex);
            auto staticIt = staticArpCache.find(targetIp);
            if (staticIt != staticArpCache.end())
            {
                std::memcpy(out, staticIt->second.macAddress, 6);
                return true;
            }
        }
        auto it = arpCache.find(targetIp);
        if (it != arpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime && it->second.status == ArpCacheStatus::COMPLETE)
        {
            std::memcpy(out, it->second.macAddress, 6);
            return true;
        }
        return false;
    }

    // Enqueue a packet for ARP resolution and send once resolved
    void Arp::resolveAndSend(const uint8_t* targetIp, PacketBuilder& packetToSend)
    {
        uint32_t targetIpInt = readU32(targetIp);

        if (!global.configs.arp.incompleteEnabled.load(std::memory_order_relaxed))
            return;

        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            auto& queue = packetQueuePerIp[targetIpInt];

            // Enforce queue size limit from global config
            if (queue.size() < global.configs.arp.queueSize.load(std::memory_order_relaxed))
            {
                packetQueuePerIp[targetIpInt].push(std::move(packetToSend));
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
            auto it = arpCache.find(targetIpInt);
            if (it != arpCache.end()) return;

            // Entry limit enforcement
            if (incompletes.load(std::memory_order_relaxed) >= global.configs.arp.incompleteResolveLimit.load(std::memory_order_relaxed))
            {
                pendingIncompletes.insert(targetIpInt);
                return; // Too many incomplete entries
            }

            // Add incomplete arp entry
            ArpCacheEntry entry;
            entry.status = ArpCacheStatus::INCOMPLETE;
            arpCache[targetIpInt] = entry;
            incompletes.fetch_add(1, std::memory_order_seq_cst);
        }

        sendRequest(targetIpInt);
    }

    void Arp::expireArpEntry(uint32_t ip)
    {
        if (global.configs.arp.incompleteEnabled.load(std::memory_order_relaxed))
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
    void Arp::sendRequest(uint32_t targetIp)
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
    void Arp::receiveReply(const ArpHeader& receivedReply)
    {
        const uint8_t* mac = receivedReply.getSenderHwAddr();
        const uint8_t* ip = receivedReply.getSenderIpAddr();
        const uint8_t* tip = receivedReply.getTargetIpAddr();

        uint32_t targetIp = readU32(ip);

        // Ignore gratuitous ARP if disabled
        bool garp = std::memcmp(ip, tip, 4) == 0;
        if (garp && (!global.configs.arp.acceptGratiutous.load(std::memory_order_relaxed) ||
            !running.load(std::memory_order_relaxed) || !global.routingEnabled ||
            std::memcmp(mac, ETHERNET_MAC_BROADCAST, 6) == 0))
            return;

        if (arpCache.count(targetIp))
        {
            if (arpCache[targetIp].status == ArpCacheStatus::COMPLETE && global.configs.arp.stickyArp.load(std::memory_order_relaxed))
            {
                // Ignore if sticky ARP is enabled
                return;
            }

            {
                std::lock_guard<std::mutex> lock(requestMutex);
                pendingRequests.erase(targetIp);
            }
            {
                std::lock_guard<std::mutex> lock(replyStatusMutex);
                replyStatus.erase(targetIp);
            }

            {
                std::unique_lock<std::shared_mutex> lock(arpCacheMutex);

                ArpCacheEntry& entry = arpCache[targetIp];
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
                    [this, targetIp](uint32_t)
                    {
                        expireArpEntry(targetIp);
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
            arpCache[targetIp] = entry;
            insertionOrder.push_back(targetIp);

            // Expire time
            entry.timerId = global.timeManager.addTimer(
                entry.expiryTime,
                [this, targetIp](uint32_t)
                {
                    expireArpEntry(targetIp);
                }
            );
        }
        else return;

        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            if (replyStatus.count(targetIp))
            {
                replyStatus[targetIp].store(true, std::memory_order_release);
            }
        }

        processQueuedPackets(ip, targetIp, mac);
    }

    void Arp::receiveRequest(const ArpHeader& request, const uint8_t* sourceMac)
    {
        uint32_t targetIp = readU32(request.raw->targetIpAddress);
        uint32_t senderIp = readU32(request.raw->senderIpAddress);

        if (currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;

        if (configs.authorized.load(std::memory_order_relaxed) && !staticArpCache.count(senderIp))
        {
            return;
        }

        // Drop invalid request (e.g., 0.0.0.0 or identical source/target)
        if (targetIp == 0 || targetIp == senderIp)
            return;

        uint8_t replyMac[6];
        bool isLocal = false;
        bool isProxy = false;

        {
            if (currentInterface->configs.ipv4.comparePrimaryAddress(request.raw->targetIpAddress))
            {
                currentInterface->configs.getMac(replyMac);
                isLocal = true;
            }
            else if (!global.configs.arp.disableProxy.load(std::memory_order_relaxed))
            {
                std::shared_lock<std::shared_mutex> arpLock(arpCacheMutex);
                auto it = proxyEntries.find(targetIp);
                if (it != proxyEntries.end())
                {
                    writeU48(replyMac, it->second);
                    isProxy = true;
                }
            }
        }

        if (!isLocal && !isProxy)
            return; // Not for us

        PacketBuilder reply(currentInterface);
        arpReply(reply, replyMac, sourceMac, request.getTargetIpAddr(), request.getSenderIpAddr());
        currentInterface->enqueuePacket(reply, sourceMac);
    }

    void Arp::processQueuedPackets(const uint8_t* targetIp, uint32_t targetIpInt, const uint8_t* macAddress)
    {
        std::queue<PacketBuilder> packets;
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            if (auto que = packetQueuePerIp.extract(targetIpInt))
            {
                packets = std::move(que.mapped());
            }
        }

        while (!packets.empty())
        {
            PacketBuilder pkt = std::move(packets.front());
            packets.pop();
            auto current = pkt.currentBuildHeader();
            if (!current) continue;

            // Continue building next header
            switch (current->next)
            {
                //TODO add more headers
                case HeaderType::ETHERNET:
                    Protocol::Ethernet::build(currentInterface, pkt, macAddress, ETHERNET_IPV6);
                    break;
                default:
                    continue;
            }
        }
    }

    void Arp::scheduleRequest(uint32_t targetIp, ArpCacheEntry& entry)
    {
        if (!running.load(std::memory_order_relaxed) || !global.routingEnabled) return;

        uint32_t interval;
        uint32_t retries;
        if (entry.status == ArpCacheStatus::INCOMPLETE)
        {
            interval = global.configs.arp.incompleteInterval.load(std::memory_order_relaxed);
            retries = global.configs.arp.incompleteRetries.load(std::memory_order_relaxed);
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
        PacketBuilder arpReq(currentInterface);
        auto& iface = currentInterface->configs;

        uint8_t mac[6], ip[4], tip[4];
        iface.getMac(mac);
        iface.ipv4.getPrimaryAddress(ip);
        writeU32(tip, targetIp);
        arpRequest(arpReq, mac, ip, tip);

        //Ethernet::build(currentInterface, arpReq, nullptr, &Variable::Mac::broadcast, Variable::Ethernet::arp);
        currentInterface->enqueuePacket(arpReq, ETHERNET_MAC_BROADCAST);

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
    void Arp::sendReply(const uint8_t* targetMac, const uint8_t* targetIp) 
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& interfaceInfo = currentInterface->configs;
            PacketBuilder replyPacket(currentInterface);

            uint8_t ip[4], mac[6];
            interfaceInfo.getMac(mac);
            interfaceInfo.ipv4.getPrimaryAddress(ip);
            arpReply(replyPacket, mac, targetMac, ip, targetIp);

            //Ethernet::build(currentInterface, replyPacket, nullptr, &targetMac, Variable::Ethernet::arp);
            currentInterface->enqueuePacket(replyPacket, targetMac);
        }
    }

    // Creates an ARP request packet
    void Arp::arpRequest(PacketBuilder& packet, const uint8_t* currentMac, const uint8_t* ip, const uint8_t* targetIp) 
    {
        EthernetHeader eth;
        ArpHeader arp;

        auto* ethEntry = packet.reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
        auto* arpEntry = packet.reserveHeader(HeaderType::ARP, ArpHeader::fixedSize);

        eth.setBuffer(ethEntry->buffer);
        arp.setBuffer(arpEntry->buffer);

        // Set up the Ethernet header for the ARP request
        eth.setDestinationMac(ETHERNET_MAC_BROADCAST);
        eth.setSourceMac(currentMac);
        eth.setType(ETHERNET_ARP);

        arp.setHardwareType(ARP_HARDWARE_ETHERNET);
        arp.setProtocolType(ETHERNET_IPV4);
        arp.setHardwareSize(0x06);
        arp.setProtocolSize(0x04);
        arp.setOpcode(ARP_OPCODE_REQUEST);
        arp.setSenderHwAddr(currentMac);
        arp.setSenderIpAddr(ip);
        arp.setTargetHwAddr(ETHERNET_MAC_SOURCE);
        arp.setTargetIpAddr(targetIp);
    }

    // Creates an ARP reply packet
    void Arp::arpReply(PacketBuilder& packet, const uint8_t* currentMac, const uint8_t* targetMac, const uint8_t* ip, const uint8_t* targetIp) 
    {
        EthernetHeader eth;
        ArpHeader arp;

        auto* ethEntry = packet.reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
        auto* arpEntry = packet.reserveHeader(HeaderType::ARP, ArpHeader::fixedSize);

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
        arp.setSenderIpAddr(ip);
        arp.setTargetHwAddr(targetMac);
        arp.setTargetIpAddr(targetIp);
    }
}
