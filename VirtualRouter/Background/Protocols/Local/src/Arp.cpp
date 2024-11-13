#include <Arp.h>

namespace Protocol 
{

    // Constructor: Initializes the ARP object with the given interface
    Arp::Arp(Interface& CurrentInterface) : currentInterface(&CurrentInterface) 
    {
        // Start ARP cache cleanup thread
        std::thread([this]() 
        {
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::seconds(30)); // Cleanup Interval
                
                std::lock_guard<std::mutex> lock(arpCacheMutex);
                auto now = std::chrono::steady_clock::now();
                for (auto it = arpCache.begin(); it != arpCache.end(); )
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
        }).detach();
    }

    // Sends an ARP request and processes the reply
    void Arp::sendRequest(std::string targetIp)
    {
        if (targetIp.empty()) {return;}

        {
            std::lock_guard<std::mutex> lock(arpCacheMutex);
            auto cacheIt = arpCache.find(targetIp);
            if (cacheIt != arpCache.end())
            {
                // Check if the cache entry is still valid
                if (std::chrono::steady_clock::now() < cacheIt->second.expiryTime)
                {
                    return;
                }
                else
                {
                    // Remove expired cache entry
                    arpCache.erase(cacheIt);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            // Check if there is already a pending request for this IP
            auto it = pendingRequests.find(targetIp);
            if (it != pendingRequests.end()) {
                return;
            }

            // Add to pending requests
            pendingRequests[targetIp] = true;
        }

        // Launch async ARP request handleer
        std::thread([this, targetIp]() {
            bool replyReceived = false;
            int retryCount = 0;
            const int maxRetries = 3;
            const std::chrono::seconds retryInterval(2);

            while(!replyReceived && retryCount < maxRetries)
            {
                ipInfo interfaceInfo = currentInterface->Get();
                PacketInfo arp = ArpRequest(interfaceInfo.mac, interfaceInfo.ip, targetIp);
                const std::string arpPacket = Encapsulate(arp);
                currentInterface->packetOutQueue.enqueue(arpPacket);

                // Simulate waiting for an ARP reply
                std::this_thread::sleep_for(retryInterval);

                // Check for ARP reply
                replyReceived = checkForReply(targetIp);
                retryCount++;
            }
            
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                pendingRequests.erase(targetIp);
            }
        }).detach();
    }

    bool Arp::checkForReply(const string& targetIp)
    {
        std::lock_guard<std::mutex> lock(replyMutex);

        // Find pending reply
        auto it = pendingReplies.find(targetIp);

        if (it != pendingReplies.end())
        {
            pendingReplies.erase(it);
            return true;
        }
        return false;
    }

    // Method for recieving arp reply
    void Arp::RecieveReply(const arpHeader recievedReply)
    {
        std::string arpIp = recievedReply.senderIpAddress;
        std::string arpMac = recievedReply.senderHardwareAddress;

        {
            std::lock_guard<std::mutex> lock(requestMutex);
            // Only process te reply if there is a pending request
            if (pendingRequests.find(arpIp) == pendingRequests.end())
            {
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(replyMutex);
            pendingReplies[arpIp] = recievedReply;
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
            std::lock_guard<std::mutex> lock(requestMutex);
            pendingRequests.erase(arpIp);
        }
        
        // Adds arp to the routing table
        RoutingTable& routingTable = RoutingTable::getInstance();
        routingTable.UpdateArp(recievedReply);
    }

    // Method to reply to arp request
    void Arp::sendReply(string targetMac, string targetIp) {
        if (targetIp == "") {return;}
        if (currentInterface->Get().ip == "") {return;}
        // Check for empty IP
        if (targetIp.empty()) {
            return;
        }

        ipInfo interfaceInfo = currentInterface->Get();
        
        // Initializations
        PacketInfo arpPacket;
        string packet;
        
        // Create an ARP reply packet
        arpPacket = ArpReply(interfaceInfo.mac, targetMac, interfaceInfo.ip, targetIp);
        packet = Encapsulate(arpPacket);

        // Ques arp packet for sending
        currentInterface->packetOutQueue.enqueue(packet);
    }

    // Creates an ARP request packet
    PacketInfo Arp::ArpRequest(std::string& currentMac, std::string& ip, std::string targetIp) 
    {
        PacketInfo packet;
        ethernetHeader eth;
        arpHeader arp;

        // Set up the Ethernet header for the ARP request
        eth.destinationMac = variable.mac.broadcast; 
        eth.sourceMac = currentMac; 
        eth.type = variable.ethernet.arp; 

        packet.Layer2.push_back(eth);

        // Set up the ARP header for the request
        arp.hardwareType = variable.arp.ethernet; 
        arp.protocolType = variable.arp.ipv4; 
        arp.hardwareSize = std::string("\x06", 1); 
        arp.protocolSize = std::string("\x04", 1); 
        arp.opcode = variable.arp.opcode.request; 
        arp.senderHardwareAddress = currentMac; 
        arp.senderIpAddress = ip; 
        arp.targetHardwareAddress = variable.mac.source; 
        arp.targetIpAddress = targetIp; 

        packet.Layer2_5.push_back(arp);

        return packet;
    }

    // Creates an ARP reply packet
    PacketInfo Arp::ArpReply(string& currentMac, string& targetmac, string& ip, string& targetIp) 
    {
        PacketInfo packet;
        ethernetHeader eth;
        arpHeader arp;

        // Set up the Ethernet header for the ARP reply
        eth.destinationMac = targetmac; 
        eth.sourceMac = currentMac;
        eth.type = variable.ethernet.arp;

        packet.Layer2.push_back(eth);

        // Set up the ARP header for the reply
        arp.hardwareType = variable.arp.ethernet; 
        arp.protocolType = variable.arp.ipv4; 
        arp.hardwareSize = std::string("\x06", 1); 
        arp.protocolSize = std::string("\x04", 1); 
        arp.opcode = variable.arp.opcode.reply; 
        arp.senderHardwareAddress = currentMac;
        arp.senderIpAddress = ip;
        arp.targetHardwareAddress = targetmac;
        arp.targetIpAddress = targetIp; 

        packet.Layer2_5.push_back(arp);

        return packet;
    }
}
