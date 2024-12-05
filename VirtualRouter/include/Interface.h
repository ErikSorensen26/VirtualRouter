#pragma once

#include <Ingress.h>
#include <Egress.h>
#include <Process.h>
#include <Encapsulation.h>
#include <Functions.h>
#include <Dhcp.h>
#include <Arp.h>
#include <Eigrp.h>

#include <map>
#include <thread>
#include <iostream>
#include <chrono>
#include <mutex>
#include <memory>

using namespace std; // Use standard namespace for convenience

namespace Protocol {
    class DhcpClient; // Forward declaration for DhcpClient class
    class Arp; // Forward declaration for Arp class
    class EigrpInterfaceInstance;
}

// Structure to hold IP address information
struct ipInfo {
    int id{};
    unsigned long bandwidth{}; // Bandwidth or speed of the interface
    unsigned long delay; // Delay of the interface
    string ip = ""; // IP address of the interface
    string ipv6 = ""; // IPv6 address of the interface
    int subnet = 32; // Subnet mask of the interface
    int v6subnet = 64; // V6 subnet mask of the interface
    string mac = ""; // MAC address of the interface
    int mtu{}; // Maximum Transmission Unit
    int ipv6FlowLabel{}; // Flow label for IPv6
};

// Interface class definition
class Interface {
public:
    // Constructor for Interface class
    Interface(string outInterface, const int inQueSiz, const int outQueSiz, std::string mac, int interfaceId, bool debug);
    // Destructor for Interface class
    ~Interface();

    // Runs when the interface state changes
    void stateChange();
    // Runs when the interface state changes
    void stateChangeV6();
    
    // Method to set IPv4 address and subnet mask
    void setIPv4(string ip, int subnet);
    // Method to set IPv6 address and subnet mask
    void setIPv6(string ip, int subnet = 64, bool eui64 = false);

    bool shutdown = true; // Flag to indicate if the interface is shutdown

    // Method to start background threads for packet processing
    void startThreads();
    // Method to stop background threads
    void stopThreads();

    // Method to get current IP address information
    ipInfo Get();

    string outInt; // Output interface identifier

    // RingBuffer to queue outgoing packets
    RingBuffer<std::string> packetOutQueue;

    // Mutexes for thread safety
    std::mutex packetInQueueMutex;
    std::mutex packetOutQueueMutex;
    std::mutex threadsRunningMutex;

    Ingress packetCapture; // Packet capture object

    string vrf = "default"; // Virtual Routing and Forwarding identifier

    // Structure to hold interface-specific information
    struct InterfaceInfo {
        struct Dhcp {
            string dhcpServer{}, // DHCP server address
                broadcast{}, // Broadcast address
                router{}; // Router address
            vector<string> dnsServer{}; // List of DNS servers
            string leaseTime{}, // Lease time for DHCP
                renewalTime{}, // Renewal time for DHCP
                rebindingTime{}; // Rebinding time for DHCP
            int subnetMask{}; // Subnet mask;
        } dhcp;
    } interfaceInfo;

    // Shared pointers to protocol classes
    std::shared_ptr<Protocol::DhcpClient> dhcp;
    std::shared_ptr<Protocol::Arp> arp;
    
    // Eigrp processes
    map<int, shared_ptr<Protocol::EigrpInterfaceInstance>> eigrpInterfaceList;

    // Method to start or stop the interface
    void Shutdown(bool shut);

    // Holds interface name
    string interfaceName{};

    std::mutex ipInfoMutex;

    char id; // Identifier for the interface
    unsigned long bandwidth{1000000}; // Bandwidth or speed of the interface
    unsigned long delay{10};
    string ipAddress  = ""; // IP address of the interface
    string ipv6Address = ""; // IPv6 address of the interface
    string macAddress = ""; // MAC address of the interface
    int mask = 32; // Subnet mask of the interface
    int v6mask = 64; // V6 subnet mask of the interface
    int mtu = 1500; // Maximum Transmission Unit
    int ipv6FlowLabel = 0; // Flow label for IPv6

    string adjacentIp{};
    bool adjacentValid = false;

private:

    Egress packetSend; // Packet sending object

    // Private methods for packet processing
    void PacketIngress(Ingress& packetCapture); // Method for packet ingress
    void PacketEgress(Egress& packetSend); // Method for packet egress
    void Process(Ingress& packetCapture); // Method for processing packets

    std::thread thread1; // Thread for packet ingress
    std::thread thread2; // Thread for packet egress
    std::thread thread3; // Thread for packet processing
    bool threadsRunning = false; // Flag to indicate if threads are running

    int inQsiz; // Size of the input queue
    int outQsiz; // Size of the output queue

    bool debug;
};

// External declarations
extern Interface* CurrentInterface; // Pointer to the current Interface object
extern map<string, std::map<int, std::shared_ptr<Interface>>> InterfaceList; // Map to store Interface objects by string key and integer ID
