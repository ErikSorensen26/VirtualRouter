#pragma once

#include <PacketStructure.h>
#include <Functions.h>
#include <Interface.h>
#include <Encapsulation.h>
#include <RoutingTable.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <time.h>
#include <map>
#include <condition_variable>
#include <functional> 

using namespace std;

// Declares interface class
class Interface;

namespace Protocol {

    // ARP class for handling ARP requests and replies
    class Arp {
    public:

        struct ArpCacheEntry {
            std::string macAddress;
            std::chrono::steady_clock::time_point expiryTime;
        };

        // Constructor that takes a reference to the current interface
        Arp(Interface& CurrentInterface);
    
        // Method to create an ARP request packet
        PacketInfo ArpRequest(string& currentMac, string& ip, string targetIp);
        
        // Method for recieving ARP reply
        void RecieveReply(const arpHeader recievedReply);

        // Method to check for reply
        bool checkForReply(const string& targetIp);
    
        // Method to create an ARP reply packet
        PacketInfo ArpReply(string& currentMac, string& targetmac, string& ip, string& targetIp);
    
        // Method to send an ARP request
        void sendRequest(std::string targetIp);

        // Method to reply to arp request
        void sendReply(string targetMac, string targetIp);
    
        // Public member for creating ARP requests
        PacketInfo createArpRequest;
    
        // Mutex for synchronizing access to ARP-related resources
        std::mutex arpMutex;
    
    private:

        std::unordered_map<std::string, ArpCacheEntry> arpCache;
        std::mutex arpCacheMutex;

        std::map<std::string, std::string> pendingRequests;
        std::mutex replyMutex;
        std::mutex requestMutex;
        std::condition_variable cv;

        map<std::string, arpHeader> pendingReplies;

        // Pointer to the singleton instance of Functions
        Functions* function = Functions::getInstance();
    
        // Variable for storing additional ARP-related data
        Variable variable;
    
        // Pointer to the interface associated with this ARP instance
        Interface* currentInterface;
    };

}
