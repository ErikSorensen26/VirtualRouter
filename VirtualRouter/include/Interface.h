// Interface.h

#ifndef INTERFACE_H
#define INTERFACE_H

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
#include <ThreadPool.hpp>
#include <string>

#include <map>
#include <thread>
#include <mutex>
#include <memory>

/**
 * @enum InterfaceType
 * @brief Enunerates the various types of network interfaces supported
 */
enum class InterfaceType
{
    UNDEFINED,          ///< Undefined interface type.
    DIALER,             ///< Dialer interface type.
    ETHERNET,           ///< Ethernet interface type.
    FAST_ETHERNET,      ///< Fast Ethernet type.
    GIGABIT_ETHERNET,   ///< Gigabit Ethernet interface type.
    LOOPBACK,           ///< Loopback interface type.
    PORT_CHANNEL,       ///< Port-channel interface type.
    TUNNEL,             ///< Tunnel interface type.
    VIRTUAL_TEMPLATE,   ///< Virtual Template interface type.
    VLAN                ///< VLAN interface type.
};

namespace Protocol {
    class Ethernet;                 ///< Forward declaration of Ethernet protocol class.
    class IPPacket;                 ///< Forward declaration of IPPacket protocol class.
    class DhcpClient;               ///< Forward declaration of DhcpClient protocol class.
    class Arp;                      ///< Forward declaration of Arp protocol class.
    struct EigrpInterfaceInstance;  ///< Forward declaration of EigrpInterfaceInstance struct.
}

/**
 * @struct IpInfo
 * @brief Stored IP address and related configuration information.
 */
struct IpInfo {
    std::shared_mutex ipMutex;      ///< Mutex for thread-safe access to IP information

    uint8_t id;                     ///< Identifier for the interface.
    InterfaceType interfaceType;    ///< Type of interface.
    uint32_t bandwidth{1000000};    ///< Bandwidth of the interface in kpbs.
    uint32_t delay{10};             ///< Delay of the interface in milliseconds.
    ByteString macAddress{};        ///< MAC address addociated with the interface.
    uint16_t mtu{1500};             ///< Maximum Transmission Unit size.

    /**
     * @struct IPv4
     * @brief Stores IPv4 address and subnet mask information.
     */
    struct IPv4
    {
        ByteString ipAddress{};     ///< IPv4 address.
        uint8_t mask{0};            ///< Subnet mask.
    } ipv4;

    /**
     * @struct IPv6
     * @brief Stores IPv6 address, subnet mask, and flow label information.
     */
    struct IPv6
    {
        ByteString ipAddress{};     ///< IPv6 address.
        uint8_t mask{0};            ///< Subnet Mask.
        uint32_t ipv6FlowLabel{0};  ///< IPv6 flow label
    }  ipv6;

    /**
     * @struct Dhcp
     * @brief Stores DHCP configuration information.
     */
    struct Dhcp {
        ByteString dhcpServer{};    ///< DHCP server address
        ByteString broadcast{};     ///< Broadcast address
        ByteString router{};        ///< Router address
        std::vector<ByteString> dnsServer{}; ///< List of DNS servers
        ByteString leaseTime{};     ///< Lease time for DHCP
        ByteString renewalTime{};   ///< Renewal time for DHCP
        ByteString rebindingTime{}; ///< Rebinding time for DHCP
        uint8_t subnetMask{};       ///< Subnet mask;
    } dhcp;
};

/**
 * @class Interface
 * @brief Represents a network interface handling packet ingress, egress, and processing.
 *
 * The interface class manages packet capture, sending, and processing for a specific network interface.
 * It handles IP configuration, MAC address management, and interacts with various network protocols.
 */
class Interface {
public:

    RingBuffer<ByteString> packetOutQueue; ///< Queue for outgoing packets.

    /**
     * @brief Constructs an Interface object.
     *
     * Initializes packet capture and sending mechanisms, sets up IP information,
     * initializes protocol objects, and starts background threads for packet handling.
     *
     * @param interfaceType The type of interface (e.g., ETHERNET, VLAN).
     * @param outInterface The name of the outgoing interface (e.g., WLAN0, ETH1).
     * @param inQeuSiz The size of the incoming packet queue.
     * @param outQueSiz Thye size of the outgoing packet queue.
     * @param mac The MAC address associated with the interface.
     * @param interfaceId The Identifier for the interface
     * @param debug Flag to enable or disable debug mode.
     */
    Interface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, uint8_t interfaceId, bool debug);

    /**
     * @brief Destructs the Interface object.
     *
     * Stops background threads and performs necessary cleanup.
     */
    ~Interface();

    /**
     * @brief Sets the IPv4 address and subnet mask for the interface.
     *
     * Updates the IPv4 configuration and sends gratuitous ARP packets to update the network.
     *
     * @param ip The IPv4 address to assign to the interface.
     * @param subnet The subnet mask for the IPv4 address.
     */
    void setIPv4(ByteString, uint8_t subnet);

    /**
     * @brief Sets the IPv6 address, subnet mask, and EUI-64 flag for interface.
     *
     * Updates the IPv6 configuration and triggers Neighbor Discovery Protocol (NDP) updates.
     *
     * @param ip The IPv6 address to assign to the interface.
     * @param subnet The subnet mask for the IPv6 address, Default to 64.
     * @param eui64 Flag indicating whether to use EUI-64 for IPv6 address generation.
     */
    void setIPv6(ByteString ip, uint8_t subnet = 64, bool eui64 = false);

    /**
     * @brief Retrieves the current IP address information
     *
     * Provides access to the shared IP information structure.
     *
     * @return std::shared_ptr<IpInfo> Shared pointer to the IP information.
     */
    std::shared_ptr<IpInfo> Get() const;

    /**
     * @brief Shuts down or restarts the interface.
     * 
     * Toggles the running state of the interface and triggers state changes for protocols.
     *
     * @param shut Boolean flag indicating whether to shut down ('true') or restart ('false').
     */
    void Shutdown(bool shut);
    bool shutdownFlag; ///< Flag indicating if the interface is in shutdown state.
    ByteString vrf = "default"; ///< Virtual Routing and Forwarding identifier.

    /**
     * @brief Enqueues a packet for sending through the interface.
     *
     * Serializes and enqueues the packet, replacing the MAC address if provided.
     *
     * @param packetInfo The packet information to be sent.
     * @param mac Optional MAC address to replace the packet's source MAC.
     */
    void enqueuePacket(PacketInfo& packetInfo, ByteString mac = "");

    //L2 Protocols
    std::shared_ptr<Protocol::Ethernet> ethernet;   ///< Ethernet Protocol handler.
    std::shared_ptr<Protocol::Arp> arp;             ///< ARP protocol handler.

    // L3 Protocols
    std::shared_ptr<Protocol::IPPacket> ipPacket;   ///< IP Packet protocol handler.

    // L4 Protocols
    std::map<uint32_t, std::shared_ptr<Protocol::EigrpInterfaceInstance>> eigrpInterfaceList; ///< EIGRP interface instance.
    std::shared_ptr<Protocol::DhcpClient> dhcp;     ///< DHCP Client protocol handler.

private:

    /**
     * @brief Handles packet ingress by capturing incoming packets.
     *
     * Continuously captures packets using the Ingress object.
     *
     * @param packetCapture Reference to the Ingress object for packet capturing.
     */
    void packetIngress();

    /**
     * @brief Handles packet egress by sending outgoing packets.
     *
     * Continuously sends packets from the outgoing queue using the Egress object.
     *
     * @param packetSend Reference to the Egress object for packet sending.
     */
    void packetEgress();
    
    /**
     * @brief Starts the background threads for packet handling.
     *
     * Launches threads for packet ingress, egress, and processing.
     */
    void process(); // Method for processing packets

    /**
     * @brief Starts the background threads for packet handling.
     *
     * Launches threads for packet ingress, egress, and processing.
     */
    void startThreads();

    /**
     * @brief Stops the background threads for packet handling.
     *
     * Signals threads to stop and joins them to ensure proper shutdown.
     */
    void stopThreads();

    /**
     * @brief Handles state changes relates to IPv4 configuration.
     *
     * Updates several protocols and the routing table based on the new IPv4 configuration.
     */
    void stateChange();

    /**
     * @brief Handles state changes related to IPv6 configuration.
     *
     * Updates several protocols and the routing table based on the new IPv6 configuration.
     */
    void stateChangeV6();

    /**
     * @brief Handles ARP resolution events.
     *
     * Updates ARP tables or triggers necessary actions when an ARP resolution is completed.
     *
     * @param ip The IP address for which ARP resolution was preformed.
     * @param mac The MAC address resolved for the given IP address.
     */
    void onArpResolved(const ByteString& ip, const ByteString& mac);

    // Member variables
    std::shared_ptr<IpInfo> configs;    ///< Shared pointer to IP configuration information.
    std::mutex ipInfoMutex;             ///< Mutex for thread-safe access to IP information.

    std::string outInt;     ///< Outgoing interface name.
    size_t inQsiz;          ///< Size of the incoming packet queue.
    size_t outQsiz;         ///< Size of the outgoing packet queue.
    bool debug;             ///< Flag indicating if debug mode is enabled.

    Ingress packetCapture;  ///< Ingress object for packet capturing.
    Egress packetSend;      ///< Egress object for packet sending.

    std::mutex threadsRunningMutex;     ///< Mutex for protecting the threadsRunning flag.
    std::mutex packetInQueueMutex;      ///< Mutex for protecting the incoming packet queue.
    std::mutex packetOutQueueMutex;     ///< Mutex for protecting the outgoing packet queue.
    std::atomic<bool> threadsRunning;   ///< Atomic flag indicating if threads are running.

    std::thread thread1;    ///< Thread for packet ingress.
    std::thread thread2;    ///< Thread for packet egress.
    std::thread thread3;    ///< Thread for packet processing.

    ThreadPool threadPool;  ///< Thread pool for handling asynchronous tasks.

    std::condition_variable packetOutQueueCV; ///< Condition variable to notify packet egress thread.
};

// External declarations
extern std::weak_ptr<Interface> currentInterface; ///< Weak pointer to the current Interface object.
extern std::shared_mutex interfaceListMutex; ///< Mutex for protecting access to the interface list.
extern std::map<InterfaceType, std::map<unsigned int, std::shared_ptr<Interface>>> interfaceList; ///< Map storing Interface objects categorized by InterfaceType and ID.

#endif // INTERFACE_H
