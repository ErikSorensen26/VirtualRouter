#include <Arp.h>
#include <Interface.h>

namespace Protocol 
{

    // Constructor: Initiates the ARP object with the given interface
    Arp::Arp(Interface& CurrentInterface) 
        : currentInterface(&CurrentInterface), running(true)
    {
        // Start ARP cache cleanup thread
        std::thread cacheThread(&Arp::arpCacheCleanupThread, this);

        {
            std::lock_guard<std::mutex> lock(threadMutex);
            threads.emplace_back(std::move(cacheThread));
        }
    }

    // Destructor
    Arp::~Arp()
    {
        shutdown();
    }

    // Shutdown method
    void Arp::shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            running.store(false);
        }
        threadCV.notify_all();
        
        {
            std::lock_guard<std::mutex> threadLock(threadMutex);
            for (auto& t : threads)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
            threads.clear();
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
    }

    void Arp::arpCacheCleanupThread()
    {
        while (running)
        {
            std::unique_lock<std::mutex> lock(requestMutex);
            threadCV.wait_for(lock, std::chrono::seconds(30));
            if (!running) break;

            // Cleanup expired entries
            auto now = std::chrono::steady_clock::now();
            std::unique_lock<std::shared_mutex> cacheLock(arpCacheMutex);
            for (auto it = arpCache.begin(); it != arpCache.end();)
            {
                if (now >= it->second.expiryTime)
                {
                    it = arpCache.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    // Check if MAC is known for the given IP
    bool Arp::isMacKnown(const ByteString& ip)
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        auto it = arpCache.find(ip);
        return (it != arpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime);
    }

    // Get MAC address for the given ip
    ByteString Arp::getMac(const ByteString& ip)
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        auto it = arpCache.find(ip);
        if (it != arpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime)
        {
            return it->second.macAddress;
        }
        return "";
    }

    // Enqueue a packet for ARP resolution and send once resolved
    void Arp::resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend)
    {
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp[targetIp].push(std::move(packetToSend));
        }
        sendRequest(targetIp);
    }

    // Send an ARP request for the given IP
    void Arp::sendRequest(const ByteString& targetIp)
    {
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            if (pendingRequests.count(targetIp)) return;
            pendingRequests.insert(targetIp);

            // Create reply status entry if not already present
            if (replyStatus.find(targetIp) == replyStatus.end())
            {
                replyStatus[targetIp] = std::make_shared<std::atomic<bool>>(false);
            }
        }

        std::thread newThread(&Arp::handleArpRequest, this, targetIp);
        
        {
            std::lock_guard<std::mutex> threadLock(threadMutex);
            threads.emplace_back(std::move(newThread));
        }
    }

    void Arp::handleArpRequest(const ByteString& targetIp)
    {
        const auto retryInterval = std::chrono::seconds(2);
        int retries = 3;

        // Ensure replyStatus[targetIp] is initialized
        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            if (replyStatus.find(targetIp) == replyStatus.end())
            {
                replyStatus[targetIp] = std::make_shared<std::atomic<bool>>(false);
            }
        }

        for (int retry = 0; retry < retries; ++retry)
        {
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                if (!running || replyStatus[targetIp]->load()) break;
            }

            PacketInfo arpReq;
            {
                auto iface = currentInterface->Get();
                if (iface)
                {
                    std::shared_lock<std::shared_mutex> lock(iface->ipMutex);
                    arpReq = arpRequest(iface->macAddress, iface->ipv4.ipAddress, targetIp);
                }
            }

            // Send the ARP request
            currentInterface->enqueuePacket(arpReq, Variable::Mac::broadcast);

            if (waitForReply(targetIp, retryInterval)) break;
        }

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(targetIp);
        }
    }

    // Method to receive ARP reply
    void Arp::receiveReply(const ArpHeader& receivedReply)
    {
        ByteString ip = receivedReply.senderIpAddress.toString();
        ByteString mac = receivedReply.senderHardwareAddress.toString();

        {
            std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
            arpCache[ip] = {mac, std::chrono::steady_clock::now() + std::chrono::seconds(60)};
        }

        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            if (replyStatus.count(ip))
            {
                replyStatus[ip]->store(true);
            }
        }

        threadCV.notify_all();
        processQueuedPackets(ip, mac);
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

        threadCV.notify_all();
    }

    bool Arp::waitForReply(const ByteString& targetIp, const std::chrono::milliseconds& timeout)
    {
        std::unique_lock<std::mutex> lock(replyStatusMutex);

        // Initialize reply static for the target IP if not already present
        if (replyStatus.count(targetIp) == 0)
        {
            replyStatus[targetIp] = std::make_shared<std::atomic<bool>>(false);
        }

        auto start = std::chrono::steady_clock::now();
        auto end = start + timeout;

        // Wait for the conditional variable to be modified or timeout
        while (std::chrono::steady_clock::now() < end)
        {
            if (threadCV.wait_until(lock, std::min(end, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)), [this, &targetIp]() {
                    return !running || (replyStatus[targetIp] && replyStatus[targetIp]->load());
                }))
            {
                return replyStatus[targetIp]->load();
            }
        }
        return false;
    }

    // Method to send an ARP reply
    void Arp::sendReply(ByteString targetMac, ByteString targetIp) 
    {
        auto interfaceInfo = currentInterface->Get();
        if (interfaceInfo)
        {
            PacketInfo replyPacket;

            {
                std::shared_lock<std::shared_mutex> lock(interfaceInfo->ipMutex);
                replyPacket = arpReply(interfaceInfo->macAddress, targetMac, interfaceInfo->ipv4.ipAddress, targetIp);
            }

            currentInterface->enqueuePacket(replyPacket, targetMac);
        }
    }

    // Creates an ARP request packet
    PacketInfo Arp::arpRequest(ByteString& currentMac, ByteString& ip, ByteString targetIp) 
    {
        PacketInfo packet;
        EthernetHeader eth;
        ArpHeader arp;

        // Set up the Ethernet header for the ARP request
        eth.destinationMac = Variable::Mac::broadcast; 
        eth.sourceMac = currentMac; 
        eth.type = Variable::Ethernet::arp; 

        packet.Layer2.push_back(eth);

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

        packet.Layer2_5.push_back(arp);

        return packet;
    }

    // Creates an ARP reply packet
    PacketInfo Arp::arpReply(ByteString& currentMac, ByteString& targetMac, ByteString& ip, ByteString& targetIp) 
    {
        PacketInfo packet;
        EthernetHeader eth;
        ArpHeader arp;

        // Set up the Ethernet header for the ARP reply
        eth.destinationMac = targetMac; 
        eth.sourceMac = currentMac;
        eth.type = Variable::Ethernet::arp;

        packet.Layer2.push_back(eth);

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

        packet.Layer2_5.push_back(arp);

        return packet;
    }

}
