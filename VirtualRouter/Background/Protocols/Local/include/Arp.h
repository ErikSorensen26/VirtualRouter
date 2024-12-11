#pragma once

#include <PacketStructure.h>
#include <Functions.h>
#include <Interface.h>
#include <Encapsulation.h>
#include <RoutingTable.h>
#include <mutex>
#include <chrono>

// Declares interface class
class Interface;

namespace Protocol {
    struct ArpCacheEntry {
        ByteString macAddress;
        std::chrono::steady_clock::time_point expiryTime;
    };

    // ARP class for handling ARP requests and replies
    class Arp {
    public:

        // Constructor that takes a reference to the current interface
        Arp(Interface& CurrentInterface);
        ~Arp() = default;
    
        // Method to equeue a packet for ARP resolution and send once resolved
        void resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend);

        // Method to receive an ARP reply
        void receiveReply(const ArpHeader& recievedReply);

        // Methods to check if MAC is known and to get MAC
        bool isMacKnown(const ByteString& ip);
        ByteString getMac(const ByteString& ip);
        PacketInfo arpReply(ByteString& currentMac, ByteString& targetMac, ByteString& ip, ByteString& targetIp);
        void sendReply(ByteString targetMac, ByteString targetIp);
        PacketInfo arpRequest(ByteString& currentMac, ByteString& ip, ByteString targetIp); 
        void sendRequest(const ByteString& targetIp);

    private:
        
        // ARP cache: Maps IP to MAC and expiry time
        std::unordered_map<ByteString, ArpCacheEntry, std::hash<ByteString>, std::equal_to<ByteString>> arpCache;
        std::mutex arpCacheMutex;

        // Pending ARP requests: Tracks ongoing ARP requests
        std::unordered_map<ByteString, bool, std::hash<ByteString>, std::equal_to<ByteString>> pendingRequests;
        std::mutex requestMutex;

        // Reply status: Indicates if a reply has been received for an IP
        std::unordered_map<ByteString, std::shared_ptr<std::atomic<bool>>, std::hash<ByteString>, std::equal_to<ByteString>> replyStatus;
        std::mutex replyStatusMutex;

        // Queues of packets waiting for ARP resolution, keyed by IP
        std::unordered_map<ByteString, std::queue<PacketInfo>, std::hash<ByteString>, std::equal_to<ByteString>> packetQueuePerIp;
        std::mutex packetQueueMutex;

        // Reference to the Interface for 
        Interface* currentInterface;
    };
}
