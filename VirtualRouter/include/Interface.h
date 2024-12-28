#pragma once

#include <Ingress.h>
#include <Egress.h>
#include <Process.h>
#include <Encapsulation.h>
#include <Functions.h>
#include <Dhcp.h>
#include <Arp.h>
#include <Eigrp.h>
#include <Ethernet.h>
#include <IPPacket.h>
#include <ThreadPool.h>
#include <string>

#include <map>
#include <thread>
#include <mutex>
#include <memory>

enum class InterfaceType
{
    UNDEFINED,
    DIALER,
    ETHERNET,
    FAST_ETHERNET,
    GIGABIT_ETHERNET,
    LOOPBACK,
    PORT_CHANNEL,
    TUNNEL,
    VIRTUAL_TEMPLATE,
    VLAN
};

namespace Protocol {
    class Ethernet;
    class IPPacket;
    class DhcpClient;
    class Arp;
    class EigrpInterfaceInstance;
}

// Structure to hold IP address information
struct IpInfo {
    std::shared_mutex ipMutex;

    int id;
    double bandwidth{1000000};
    double delay{10};
    ByteString macAddress{};
    int mtu{1500};

    struct IPv4
    {
        ByteString ipAddress{};
        int mask{0};
    } ipv4;

    struct IPv6
    {
        ByteString ipAddress{};
        int mask{0};
        int ipv6FlowLabel{0};
    }  ipv6;

    struct Dhcp {
        ByteString dhcpServer{}, // DHCP server address
            broadcast{}, // Broadcast address
            router{}; // Router address
        std::vector<std::string> dnsServer{}; // List of DNS servers
        ByteString leaseTime{}, // Lease time for DHCP
            renewalTime{}, // Renewal time for DHCP
            rebindingTime{}; // Rebinding time for DHCP
        int subnetMask{}; // Subnet mask;
    } dhcp;
};

// Interface class definition
class Interface {
public:

    // Out queue
    RingBuffer<ByteString> packetOutQueue;

    // Constructor for Interface class
    Interface(InterfaceType interfaceType, std::string outInterface, const int inQueSiz, const int outQueSiz, std::string mac, int interfaceId, bool debug);
    // Destructor for Interface class
    ~Interface();

    // Method to set IPv4 and IPv6
    void setIPv4(std::string ip, int subnet);
    void setIPv6(std::string ip, int subnet = 64, bool eui64 = false);

    // Method to get current IP address information
    std::shared_ptr<IpInfo> Get() const;

    // Method to start or stop the interface
    void Shutdown(bool shut);
    bool shutdownFlag;
    ByteString vrf = "default";

    // Thread safe packet enqueuing
    void enqueuePacket(PacketInfo& packetInfo, ByteString mac = "");

    // L2 Protocols
    std::shared_ptr<Protocol::Ethernet> ethernet;
    std::shared_ptr<Protocol::Arp> arp;

    // L3 Protocols
    std::shared_ptr<Protocol::IPPacket> ipPacket;

    // L4 Protocols
    std::map<int, std::shared_ptr<Protocol::EigrpInterfaceInstance>> eigrpInterfaceList;
    std::shared_ptr<Protocol::DhcpClient> dhcp;

private:

    std::shared_ptr<IpInfo> configs;
    std::mutex ipInfoMutex;

    // Private methods for packet processing
    void packetIngress(Ingress& packetCapture); // Method for packet ingress
    void packetEgress(Egress& packetSend); // Method for packet egress
    void process(Ingress& packetCapture); // Method for processing packets

    // Helper methods
    void startThreads();
    void stopThreads();
    void stateChange();
    void stateChangeV6();

    // Method to handle ARP resolution
    void onArpResolved(const ByteString& ip, const ByteString& mac);

    // Members
    std::string outInt;
    int inQsiz;
    int outQsiz;
    bool debug;

    Ingress packetCapture;
    Egress packetSend;

    std::mutex threadsRunningMutex;
    std::mutex packetInQueueMutex;
    std::mutex packetOutQueueMutex;
    std::atomic<bool> threadsRunning;

    std::thread thread1;
    std::thread thread2;
    std::thread thread3;

    ThreadPool threadPool;

    // Condition variable to notify packet egress thread
    std::condition_variable packetOutQueueCV;
};

// External declarations
extern std::weak_ptr<Interface> currentInterface; // Pointer to the current Interface object
extern std::shared_mutex interfaceListMutex;
extern std::map<InterfaceType, std::map<int, std::shared_ptr<Interface>>> interfaceList; // Map to store Interface objects by string key and integer ID
