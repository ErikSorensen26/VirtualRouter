#include <Arp.h>
#include <Interface.h>
#include <Ethernet.h>
#include <VirtualRouter.h>
#include <Global.h>

namespace Protocol 
{

    // Constructor: Initiates the ARP object with the given interface
    Arp::Arp(Interface& CurrentInterface) 
        : currentInterface(&CurrentInterface),
        global(CurrentInterface.routingInstance->global)
    {
        initiateArp();
    }

    void Arp::initiateArp()
    {
        std::shared_lock<std::shared_mutex> lock(global.configs.arp.neighborMutex);
        auto it = global.configs.arp.neighbors.find(currentInterface->routingInstance->instanceName);
        if (it != global.configs.arp.neighbors.end())
        {
            for (const auto& [ip, neighbor] : it->second)
            {
                if (neighbor.interface.first == currentInterface->configs.interfaceType &&
                    neighbor.interface.second == currentInterface->configs.id)
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

    void Arp::addArpEntry(const ByteString& targetIp, const ByteString& targetMac, bool proxy, bool isStatic)
    {
        auto now = std::chrono::steady_clock::now();

        if (isStatic)
        {
            ArpCacheEntry entry;
            entry.macAddress = targetMac;
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
        entry.macAddress = targetMac;
        entry.expiryTime = now + std::chrono::seconds(configs.timeout);

        // Refresh vs expire logic
        entry.timerId = global.timeManager.addTimer(
            entry.expiryTime,
            [this, targetIp]() { expireArpEntry(targetIp); }
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

    void Arp::removeArpEntry(const ByteString& ip, bool isStatic)
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
    ByteString* Arp::getMac(const ByteString& ip)
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        {
            std::shared_lock<std::shared_mutex> neighborLock(global.configs.arp.neighborMutex);
            auto staticIt = staticArpCache.find(ip);
            if (staticIt != staticArpCache.end())
            {
                return &staticIt->second.macAddress;
            }
        }
        auto it = arpCache.find(ip);
        if (it != arpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime && it->second.status == ArpCacheStatus::COMPLETE)
        {
            return &it->second.macAddress;
        }
        return nullptr;
    }

    // Enqueue a packet for ARP resolution and send once resolved
    void Arp::resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend)
    {
        if (!global.configs.arp.incompleteEnabled.load(std::memory_order_relaxed))
            return;

        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            auto& queue = packetQueuePerIp[targetIp];

            // Enforce queue size limit from global config
            if (queue.size() < global.configs.arp.queueSize.load(std::memory_order_relaxed))
            {
                packetQueuePerIp[targetIp].push(std::move(packetToSend));
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
            auto it = arpCache.find(targetIp);
            if (it != arpCache.end()) return;

            // Entry limit enforcement
            if (incompletes.load(std::memory_order_relaxed) >= global.configs.arp.incompleteResolveLimit.load(std::memory_order_relaxed))
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

    void Arp::expireArpEntry(const ByteString& ip)
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
    void Arp::sendRequest(const ByteString& targetIp)
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
        ByteString ip = receivedReply.senderIpAddress;
        ByteString mac = receivedReply.senderHardwareAddress;

        // Ignore gratuitous ARP if disabled
        bool garp = receivedReply.senderIpAddress == receivedReply.targetIpAddress;
        if (garp && (!global.configs.arp.acceptGratiutous.load(std::memory_order_relaxed) ||
            !running.load(std::memory_order_relaxed) ||
            mac == Variable::Mac::broadcast || mac == Variable::Mac::source))
            return;

        if (arpCache.contains(ip))
        {
            if (arpCache[ip].status == ArpCacheStatus::COMPLETE && global.configs.arp.stickyArp.load(std::memory_order_relaxed))
            {
                // Ignore if sticky ARP is enabled
                return;
            }

            {
                std::lock_guard<std::mutex> lock(requestMutex);
                pendingRequests.erase(ip);
            }
            {
                std::lock_guard<std::mutex> lock(replyStatusMutex);
                replyStatus.erase(ip);
            }

            {
                std::unique_lock<std::shared_mutex> lock(arpCacheMutex);

                ArpCacheEntry& entry = arpCache[ip];
                entry.status = ArpCacheStatus::COMPLETE;
                entry.macAddress = mac;
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
                    [this, ip]()
                    {
                        expireArpEntry(ip);
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
            entry.macAddress = mac;
            entry.expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(configs.timeout);
            arpCache[ip] = entry;
            insertionOrder.push_back(ip);

            // Expire time
            entry.timerId = global.timeManager.addTimer(
                entry.expiryTime,
                [this, ip]()
                {
                    expireArpEntry(ip);
                }
            );
        }
        else return;

        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            if (replyStatus.count(ip))
            {
                replyStatus[ip].store(true, std::memory_order_release);
            }
        }

        processQueuedPackets(ip, mac);
    }

    void Arp::receiveRequest(const ArpHeader& request, const ByteString& sourceMac)
    {
        const ByteString& targetIp = request.targetIpAddress;
        const ByteString& senderIp = request.senderIpAddress;

        if (currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;

        if (configs.authorized.load(std::memory_order_relaxed) && !staticArpCache.count(senderIp))
        {
            return;
        }

        // Drop invalid request (e.g., 0.0.0.0 or identical source/target)
        if (targetIp.empty() || targetIp == senderIp)
            return;

        ByteString replyMac;
        bool isLocal = false;
        bool isProxy = false;

        {
            if (currentInterface->configs.ipv4.getAddress() == targetIp)
            {
                replyMac = currentInterface->configs.getMac();
                isLocal = true;
            }
            else if (!global.configs.arp.disableProxy.load(std::memory_order_relaxed))
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

        PacketInfo reply = arpReply(replyMac, sourceMac, targetIp, senderIp);
        currentInterface->enqueuePacket(reply, sourceMac);
    }

    void Arp::processQueuedPackets(const ByteString& targetIp, const ByteString& macAddress)
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

    void Arp::scheduleRequest(const ByteString& targetIp, ArpCacheEntry& entry)
    {
        if (!running.load(std::memory_order_relaxed)) return;

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
        PacketInfo arpReq;
        auto& iface = currentInterface->configs;
        arpReq = arpRequest(iface.getMac(), iface.ipv4.getAddress(), targetIp);
        //Ethernet::build(currentInterface, arpReq, nullptr, &Variable::Mac::broadcast, Variable::Ethernet::arp);
        currentInterface->enqueuePacket(arpReq, Variable::Mac::broadcast);

        // Wait for the conditional variable to be modified or timeout
        uint32_t timerId = global.timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::seconds(interval),
            [this, targetIp, &entry]() {
                scheduleRequest(targetIp, entry);
            }
        );
        {
            std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
            entry.timerId = timerId;
        }
    }

    // Method to send an ARP reply
    void Arp::sendReply(const ByteString& targetMac, const ByteString& targetIp) 
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& interfaceInfo = currentInterface->configs;
            PacketInfo replyPacket;

            {
                replyPacket = arpReply(interfaceInfo.getMac(), targetMac, interfaceInfo.ipv4.getAddress(), targetIp);
            }

            //Ethernet::build(currentInterface, replyPacket, nullptr, &targetMac, Variable::Ethernet::arp);
            currentInterface->enqueuePacket(replyPacket, targetMac);
        }
    }

    // Creates an ARP request packet
    PacketInfo Arp::arpRequest(const ByteString& currentMac, const ByteString& ip, const ByteString targetIp) 
    {
        PacketInfo packet;
        EthernetHeader eth;
        ArpHeader arp;

        // Set up the Ethernet header for the ARP request
        eth.destinationMac = Variable::Mac::broadcast; 
        eth.sourceMac = currentMac; 
        eth.type = Variable::Ethernet::arp; 

        packet.Layer2.push_back(std::move(eth));

        // Set up the ARP header for the request
        arp.hardwareType = Variable::Arp::ethernet; 
        arp.protocolType = Variable::Arp::ipv4; 
        arp.hardwareSize = std::string("\x06", 1); 
        arp.protocolSize = std::string("\x04", 1); 
        arp.opcode = Variable::Arp::Opcode::request;
        arp.senderHardwareAddress = currentMac; 
        arp.senderIpAddress = ip; 
        arp.targetHardwareAddress = Variable::Mac::source; 
        arp.targetIpAddress = targetIp; 

        packet.Layer2_5.push_back(std::move(arp));

        return packet;
    }

    // Creates an ARP reply packet
    PacketInfo Arp::arpReply(const ByteString& currentMac, const ByteString& targetMac, const ByteString& ip, const ByteString& targetIp) 
    {
        PacketInfo packet;
        EthernetHeader eth;
        ArpHeader arp;

        // Set up the Ethernet header for the ARP reply
        eth.destinationMac = targetMac; 
        eth.sourceMac = currentMac;
        eth.type = Variable::Ethernet::arp;

        packet.Layer2.push_back(std::move(eth));

        // Set up the ARP header for the reply
        arp.hardwareType = Variable::Arp::ethernet; 
        arp.protocolType = Variable::Arp::ipv4; 
        arp.hardwareSize = std::string("\x06", 1); 
        arp.protocolSize = std::string("\x04", 1); 
        arp.opcode = Variable::Arp::Opcode::reply; 
        arp.senderHardwareAddress = currentMac;
        arp.senderIpAddress = ip;
        arp.targetHardwareAddress = targetMac;
        arp.targetIpAddress = targetIp; 

        packet.Layer2_5.push_back(std::move(arp));

        return packet;
    }
}
