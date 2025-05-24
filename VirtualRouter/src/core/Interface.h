// Interface.h

#ifndef INTERFACE_H
#define INTERFACE_H

#include <Ingress.h>
#include <Egress.h>
#include <Process.h>
#include <Encapsulation.h>
#include <Functions.h>
#include <ThreadPool.hpp>
#include <string>
#include <TimeManager.h>
#include <InterfaceConfigs.h>

#include <map>
#include <thread>
#include <mutex>

class EigrpTest;
class VirtualRouter;
class MockInterface;
enum class InterfaceType;

/**
 * @enum StateChange
 * @brief Indicates the type of state change.
 */
enum class StateChange
{
    SHUTDOWN,
    INITIATE,
    IPCHANGE,
    IPREMOVAL
};

namespace Protocol 
{
    class Ethernet;                 ///< Forward declaration of Ethernet protocol class.
    class DhcpClient;               ///< Forward declaration of DhcpClient protocol class.``
    class Dhcpv6Client;             ///< Forward declaration of Dhcpv6Client protocol class.``
    class Arp;                      ///< Forward declaration of Arp protocol class.
    class Ndp;                      ///< Forward declaration of Arp protocol class.
    struct EigrpInterfaceInstance;  ///< Forward declaration of EigrpInterfaceInstance struct.
}

namespace EigrpConfigs 
{
    struct InterfaceConfigs;
}


/**
 * @class Interface
 * @brief Represents a network interface handling packet ingress, egress, and processing.
 *
 * The interface class manages packet capture, sending, and processing for a specific network interface.
 * It handles IP configuration, MAC address management, and interacts with various network protocols.
 */
class Interface
{
public:
    friend class ::MockInterface;
    friend class ::EigrpTest;
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
    Interface(
        InterfaceType interfaceType,
        std::string outInterface,
        const size_t inQueSiz,
        const size_t outQueSiz,
        std::string mac,
        float interfaceId,
        VirtualRouter& vrf,
        bool debug = false
    );

    /**
     * @brief Destructs the Interface object.
     *
     * Stops background threads and performs necessary cleanup.
     */
    virtual ~Interface();

    /**
     * @brief Cleans up all of the interface objects.
     */
    void cleanupInterface();

    /**
     * @brief Sets the IPv4 address and subnet mask for the interface.
     *
     * Updates the IPv4 configuration and sends gratuitous ARP packets to update the network.
     *
     * @param ip The IPv4 address to assign to the interface.
     * @param subnet The subnet mask for the IPv4 address.
     */
    virtual void setIPv4(ByteString, uint8_t subnet);

    /**
     * @brief Sets the IPv6 address, subnet mask, and EUI-64 flag for interface.
     *
     * Updates the IPv6 configuration and triggers Neighbor Discovery Protocol (NDP) updates.
     *
     * @param ip The IPv6 address to assign to the interface.
     * @param linkLocal Indicates if the address is linkLocal
     * @param subnet The subnet mask for the IPv6 address, Default to 64.
     * @param eui64 Flag indicating whether to use EUI-64 for IPv6 address generation.
     */
    virtual void setIPv6(ByteString ip, bool linkLocal = false, uint8_t subnet = 64, bool eui64 = false);

    /**
     * @brief Removes the IPv4 address and subnet mask.
     */
    void removeIPv4();

    /**
     * @brief Removes the IPv6 address and subnet mask.
     *
     * @param linkLocal Indicates if the address is link-local.
     */
    void removeIPv6(const ByteString& ip, bool linkLocal = false);

    /**
     * @brief Gathers and returns all tentative addresses on the interface.
     *
     * Helper address to return all pending IPv6 addresses.
     */
    std::vector<ByteString> getTentativeAddress();

    /**
     * @brief Marks a IPv6 address as a duplicate making it invalid.
     *
     * @param address IPv6 address being marked as a duplicate
     * @param optional param stating if its a link-local address or not.
     */
    void markAddressDuplicate(const ByteString& address, bool linkLocal = false);

    /**
     * @brief Shuts down or restarts the interface.
     * 
     * Toggles the running state of the interface and triggers state changes for protocols.
     *
     * @param shut Boolean flag indicating whether to shut down ('true') or restart ('false').
     */
    virtual void Shutdown(bool shut);

    /**
     * @brief Enqueues a packet for sending through the interface.
     *
     * Serializes and enqueues the packet, replacing the MAC address if provided.
     *
     * @param packetInfo The packet information to be sent.
     * @param mac Optional MAC address to replace the packet's source MAC.
     */
    virtual void enqueuePacket(PacketInfo& packetInfo, ByteString mac = "");

    std::atomic<bool> shutdownFlag = false; ///< Flag indicating if the interface is in shutdown state.
    VirtualRouter* routingInstance = nullptr;

    // Member Variables
    InterfaceConfigs configs;         ///< Pointer to IP configuration information.

    Protocol::Arp* arp = nullptr;     ///< ARP protocol handler.
    Protocol::Ndp* ndp = nullptr;     ///< NDP protocol handler.

    // L4 Protocols
    std::map<uint32_t, Protocol::EigrpInterfaceInstance*> eigrpInterfaceList; ///< EIGRP interface instance.
    EigrpConfigs::InterfaceConfigs* getEigrpConfig(uint32_t as, AddressFamily af, bool negate);

    // L5 Protocols
    Protocol::DhcpClient* dhcp = nullptr;     ///< DHCP Client protocol handler.
    //Protocol::Dhcpv6Client* dhcpv6 = nullptr; ///< Dhcpv6 client protocol handler.

    /**
     * @brief Stops the background threads for packet handling.
     *
     * Signals threads to stop and joins them to ensure proper shutdown.
     */
    void stopThreads();

    /**
     * @brief Starts the background threads for packet handling.
     *
     * Launches threads for packet ingress, egress, and processing.
     */
    virtual void startThreads();

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
     * @brief Handles state changes relates to IPv4 configuration.
     *
     * Updates several protocols and the routing table based on the new IPv4 configuration.
     *
     * @param shut Indicates if the interface is shutting down or starting.
     */
    void stateChange(StateChange state);

    /**
     * @brief Handles state changes related to IPv6 configuration.
     *
     * Updates several protocols and the routing table based on the new IPv6 configuration.
     *
     * @param shut Indicates if the interface is shutting down or starting.
     */
    void stateChangeV6(StateChange state);

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

    std::condition_variable packetOutQueueCV; ///< Condition variable to notify packet egress thread.
};

// External declarations
extern Interface* currentInterface; ///< Pointer to the current Interface object.

#endif // INTERFACE_H
