#include <Arp.h>

namespace Protocol 
{

    // Constructor: Initiates the ARP object with the given interface
    Arp::Arp(Interface& CurrentInterface) : currentInterface(&CurrentInterface)
    {
        // Start ARP cache cleanup thread
        std::thread([this]()
        {
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::seconds(30));

                std::lock_guard<std::mutex> lock(arpCacheMutex);
                auto now = std::chrono::steady_clock::now();
                for (auto it = arpCache.begin(); it != arpCache.end();)
                {
                    if (now >= it->second.expiryTime)
                    {
                        Logger::getInstance().info() << "Removing expired ARP cache entry for IP " << it->first.toHex() << std::endl;
                        it = arpCache.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }).detach();
    }

    // Check if MAC is known for the given IP
    bool Arp::isMacKnown(const ByteString& ip)
    {
        std::lock_guard<std::mutex> lock(arpCacheMutex);
        auto it = arpCache.find(ip);
        if (it != arpCache.end())
        {
            if (std::chrono::steady_clock::now() < it->second.expiryTime)
            {
                return true;
            }
            else
            {
                // Expired, remove from cache
                arpCache.erase(it);
            }
        }
        return false;
    }

    // Get MAC address for the given ip
    ByteString Arp::getMac(const ByteString& ip)
    {
        std::lock_guard<std::mutex> lock(arpCacheMutex);
        auto it = arpCache.find(ip);
        if (it != arpCache.end())
        {
            if (std::chrono::steady_clock::now() < it->second.expiryTime)
            {
                return it->second.macAddress;
            }
            else
            {
                // Expired, remove from cache
                arpCache.erase(it);
            }
        }
        return "";
    }

    // Enqueue a packet for ARP resolution and send once resolved
    void Arp::resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend)
    {
        if (targetIp.empty()) {return;}

        // Enqueue the packet to the queue for the target IP
        {
            std::lock_guard<std::mutex> lock(packetQueueMutex);
            packetQueuePerIp[targetIp].push(std::move(packetToSend));
        }

        // Initiates ARP request if not already running
        sendRequest(targetIp);
    }

    // Send an ARP request for the given IP
    void Arp::sendRequest(const ByteString& targetIp)
    {
        if (targetIp.empty()) {return;}

        // Check if MAC is already known
        {
            std::lock_guard<std::mutex> lock(arpCacheMutex);
            auto cacheIt = arpCache.find(targetIp);
            if (cacheIt != arpCache.end())
            {
                if (std::chrono::steady_clock::now() > cacheIt->second.expiryTime)
                {
                    // MAC is known, send all queued packets
                    std::queue<PacketInfo> packets;
                    {
                        std::lock_guard<std::mutex> lock2(packetQueueMutex);
                        packets = std::move(packetQueuePerIp[targetIp]);
                        packetQueuePerIp.erase(targetIp);
                    }

                    while (!packets.empty())
                    {
                        PacketInfo pkt = packets.front();
                        packets.pop();
                        currentInterface->enqueuePacket(pkt, cacheIt->second.macAddress);
                        Logger::getInstance().info() << "Sending queued packet to MAC " << cacheIt->second.macAddress << " for IP " << targetIp << std::endl;
                    }
                    return;
                }
                else
                {
                    // Removed expired cache entry
                    arpCache.erase(cacheIt);
                }
            }
        }

        // Check if an ARP request is already pending
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            if (pendingRequests.find(targetIp) != pendingRequests.end())
            {
                // ARP request already pending
                Logger::getInstance().info() << "ARP request already pending for IP " << targetIp << std::endl;
                return;
            }

            // Mark as pending
            pendingRequests[targetIp] = true;
        }

        // Launch async ARP request handler
        std::thread([this, targetIp]() {
            int retryCount = 0;
            const int maxRetries = 3;
            const std::chrono::seconds retryInterval(2);

            while (retryCount < maxRetries)
            {
                // Create and send ARP request
                ipInfo interfaceInfo = currentInterface->Get();
                ByteString mac = currentInterface->Get().macAddress;
                PacketInfo arpReq = arpRequest(mac, interfaceInfo.ipAddress, targetIp);
                currentInterface->enqueuePacket(arpReq);

                Logger::getInstance().info() << "Sent ARP request for IP " << targetIp << std::endl;

                // Wait for ARP reply
                bool replyReceived = false;
                for (int i = 0; i < retryInterval.count() * 10; ++i)
                {
                    {
                        std::lock_guard<std::mutex> lock(replyStatusMutex);
                        auto it = replyStatus.find(targetIp);
                        if (it != replyStatus.end() && it->second->load())
                        {
                            replyReceived = true;
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (replyReceived)
                {
                    // Reply received, send queued packets
                    std::queue<PacketInfo> packets;
                    {
                        std::lock_guard<std::mutex> lock(packetQueueMutex);
                        packets = std::move(packetQueuePerIp[targetIp]);
                        packetQueuePerIp.erase(targetIp);
                    }

                    // Retreive MAC from cache
                    ByteString mac;
                    {
                        std::lock_guard<std::mutex> lock(arpCacheMutex);
                        auto it = arpCache.find(targetIp);
                        if (it != arpCache.end())
                        {
                            mac = it->second.macAddress;
                        }
                    }

                    while (!packets.empty())
                    {
                        PacketInfo pkt = packets.front();
                        packets.pop();
                        currentInterface->enqueuePacket(pkt, mac);
                        Logger::getInstance().info() << "Sending queued packet to MAC " << mac << " for IP " << targetIp << std::endl;
                    }

                    // Clean up
                    {
                        std::lock_guard<std::mutex> lock(requestMutex);
                        pendingRequests.erase(targetIp);
                    }

                    return;
                }
                else
                {
                    retryCount++;
                    Logger::getInstance().warn() << "ARP request failed for IP " << targetIp << ", retrying (" << retryCount << ")" << std::endl;
                }
            }

            // After retrues, give up
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                pendingRequests.erase(targetIp);
            }

            Logger::getInstance().error() << "ARP resolution failed for IP " << targetIp << " after " << maxRetries << " retries." << std::endl;
            
            // Optionally, handle failed ARP resolution (e.g., notify Interface or drop packets)
            {
                std::lock_guard<std::mutex> lock(packetQueueMutex);
                packetQueuePerIp.erase(targetIp);
            }
        }).detach();
    }

    // Method to receive ARP reply
    void Arp::receiveReply(const ArpHeader& receivedReply)
    {
        ByteString arpIp = receivedReply.senderIpAddress.toString();
        ByteString arpMac = receivedReply.senderHardwareAddress.toString();

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            // Only process the reply if there is a pending request
            if (pendingRequests.find(arpIp) == pendingRequests.end())
            {
                Logger::getInstance().warn() << "Received unsolicited ARP reply for IP " << arpIp << std::endl;
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(arpCacheMutex);
            // Add to ARP cache with expiry time
            arpCache[arpIp] = ArpCacheEntry{
                .macAddress = arpMac,
                .expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(60)
            };
        }

        {
            std::lock_guard<std::mutex> lock(replyStatusMutex);
            if (replyStatus.find(arpIp) != replyStatus.end())
            {
                replyStatus[arpIp]->store(true);
            }
            else
            {
                std::shared_ptr<std::atomic<bool>> bol = std::make_shared<std::atomic<bool>>(true);
                replyStatus.emplace(arpIp, bol);
            }
        }

        // Add ARP entry to the routing table
        RoutingTable& routingTable = RoutingTable::getInstance();
        routingTable.updateArp(receivedReply);

        Logger::getInstance().info() << "Received ARP reply: IP " << arpIp << " -> MAC " << arpMac << std::endl;
    }

    // Method to send an ARP reply
    void Arp::sendReply(ByteString targetMac, ByteString targetIp) 
    {
        if (targetIp.empty()) { return; }
        if (currentInterface->Get().ipAddress.empty()) { return; }

        ipInfo interfaceInfo = currentInterface->Get();
        
        // Create an ARP reply packet
        PacketInfo arpPacket = arpReply(interfaceInfo.macAddress, targetMac, interfaceInfo.ipAddress, targetIp);

        // Enqueue ARP reply for sending
        currentInterface->enqueuePacket(arpPacket);

        Logger::getInstance().info() << "Sent ARP reply to MAC " << targetMac.toString() << " for IP " << targetIp.toString() << std::endl;
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
