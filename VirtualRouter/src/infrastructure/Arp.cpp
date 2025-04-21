#include <Arp.h>
#include <Interface.h>
#include <Ethernet.h>

namespace Protocol 
{

    // Constructor: Initiates the ARP object with the given interface
    Arp::Arp(Interface& CurrentInterface) 
        : currentInterface(&CurrentInterface), running(false)
    {
        initiateArp();
    }

    void Arp::initiateArp()
    {
        if (running.load(std::memory_order_relaxed)) return;
        running.store(true, std::memory_order_relaxed);
        // Start ARP cache cleanup thread
        std::thread cacheThread(&Arp::arpCacheCleanupThread, this);

        {
            std::lock_guard<std::mutex> lock(threadMutex);
            threads.push_back(std::move(cacheThread));
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
        if (!running.load(std::memory_order_relaxed)) return;
        running.store(false, std::memory_order_release);
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
            threadCV.wait_for(lock, std::chrono::milliseconds(cleanTimeout));
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

    // Get MAC address for the given ip
    ByteString* Arp::getMac(const ByteString& ip)
    {
        std::shared_lock<std::shared_mutex> lock(arpCacheMutex);
        auto it = arpCache.find(ip);
        if (it != arpCache.end() && std::chrono::steady_clock::now() < it->second.expiryTime)
        {
            return &it->second.macAddress;
        }
        return nullptr;
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
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                if (pendingRequests.count(targetIp)) return;
                pendingRequests.insert(targetIp);
            }

            {
                // Create reply status entry if not already present
                std::lock_guard<std::mutex> lock(replyStatusMutex);
                if (replyStatus.find(targetIp) == replyStatus.end())
                {
                    replyStatus[targetIp] = std::make_shared<std::atomic<bool>>(false);
                }
            }
        }

        std::thread newThread(&Arp::handleArpRequest, this, targetIp);
        
        {
            std::lock_guard<std::mutex> threadLock(threadMutex);
            threads.push_back(std::move(newThread));
        }
    }

    void Arp::handleArpRequest(const ByteString& targetIp)
    {
        const auto retryInterval = std::chrono::milliseconds(retryTime);
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
                std::lock_guard<std::mutex> lock(replyStatusMutex);
                if (!running || replyStatus[targetIp]->load(std::memory_order_relaxed)) break;
            }

            PacketInfo arpReq;
            {
                if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
                {
                    auto& iface = currentInterface->configs;
                    std::shared_lock<std::shared_mutex> lock(iface.ipMutex);
                    arpReq = arpRequest(iface.macAddress, iface.ipv4.ipAddress, targetIp);
                }
            }

            // Send the ARP request
            //Ethernet::build(currentInterface, arpReq, nullptr, &Variable::Mac::broadcast, Variable::Ethernet::arp);
            currentInterface->enqueuePacket(arpReq, Variable::Mac::broadcast);

            if (waitForReply(targetIp, retryInterval))
            {
                {
                    std::lock_guard<std::mutex> lock(replyStatusMutex);
                    replyStatus.erase(targetIp);
                }
                {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    pendingRequests.erase(targetIp);
                }
                break;
            }
        }
    }

    // Method to receive ARP reply
    void Arp::receiveReply(const ArpHeader& receivedReply)
    {
        ByteString ip = receivedReply.senderIpAddress.toString();
        ByteString mac = receivedReply.senderHardwareAddress.toString();

        {
            std::unique_lock<std::shared_mutex> lock(arpCacheMutex);
            arpCache[ip] = {mac, std::chrono::steady_clock::now() + std::chrono::milliseconds(replyTimeout)};
        }

        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            if (replyStatus.count(ip))
            {
                replyStatus[ip]->store(true, std::memory_order_release);
            }
        }

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
                    return !running || (replyStatus[targetIp] && replyStatus[targetIp]->load(std::memory_order_relaxed));
                }))
            {
                return replyStatus[targetIp]->load(std::memory_order_relaxed);
            }
        }
        return false;
    }

    // Method to send an ARP reply
    void Arp::sendReply(const ByteString& targetMac, const ByteString& targetIp) 
    {
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            auto& interfaceInfo = currentInterface->configs;
            PacketInfo replyPacket;

            {
                std::shared_lock<std::shared_mutex> lock(interfaceInfo.ipMutex);
                replyPacket = arpReply(interfaceInfo.macAddress, targetMac, interfaceInfo.ipv4.ipAddress, targetIp);
            }

            //Ethernet::build(currentInterface, replyPacket, nullptr, &targetMac, Variable::Ethernet::arp);
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
