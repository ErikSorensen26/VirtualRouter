// Interface.h

#ifndef INTERFACE_H
#define INTERFACE_H

// Standard includes
#include <mutex>
#include <map>
#include <atomic>

#include <Ingress.h>
#include <Egress.hpp>
#include <InterfaceConfigs.h>
#include <PacketStructure.h>
#include <FIFOQueue.hpp>

#include <Ingress.h>

class EigrpTest;
class VirtualRouter;
class MockInterface;
class PacketBuilder;
class TxDistributor;
struct HwIfaceInfo;

enum class InterfaceType : uint8_t;
enum class StateChange
{
    SHUTDOWN,
    INITIATE,
    IPCHANGE,
    IPREMOVAL
};

namespace Eigrp
{
    struct EigrpInterfaceInstance;  ///< Forward declaration of EigrpInterfaceInstance struct.
}

namespace Protocol 
{
    class Ethernet;                 ///< Forward declaration of Ethernet protocol class.
    class DhcpClient;               ///< Forward declaration of DhcpClient protocol class.``
    class Dhcpv6Client;             ///< Forward declaration of Dhcpv6Client protocol class.``
    class Arp;                      ///< Forward declaration of Arp protocol class.
    class Ndp;                      ///< Forward declaration of Arp protocol class.
}

namespace EigrpConfigs 
{
    struct InterfaceConfigs;
}

struct InterfaceCreation
{
    InterfaceType interfaceType;
    float interfaceId;
    VirtualRouter& vrf;
    const HwIfaceInfo& info;
    bool debug;
};


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


    Interface(const InterfaceCreation& cfgs);
    virtual ~Interface();

    void cleanupInterface();

    virtual void setIPv4(uint32_t ip, uint8_t subnet);
    virtual void setIPv6(const uint8_t* ip, bool linkLocal = false, uint8_t subnet = 64, bool eui64 = false);
    void removeIPv4();
    void removeIPv6(const uint8_t* ip = nullptr);

    std::vector<std::array<uint8_t, 16>> getTentativeAddress();
    void markAddressDuplicate(const uint8_t* address, bool linkLocal = false);

    virtual void Shutdown(bool shut);
    virtual void enqueuePacket(PacketBuilder& packetInfo, const uint8_t* mac = nullptr);

    std::atomic<bool> shutdownFlag = false; ///< Flag indicating if the interface is in shutdown state.
    VirtualRouter* routingInstance = nullptr;

    // Member Variables
    InterfaceConfigs configs;         ///< IP configuration information.

    Protocol::Arp* arp = nullptr;     ///< ARP protocol handler.
    Protocol::Ndp* ndp = nullptr;     ///< NDP protocol handler.

    // L4 Protocols
    std::map<uint32_t, Eigrp::EigrpInterfaceInstance*> eigrpInterfaceList; ///< EIGRP interface instance.
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

    TxDistributor* tx;      ///< Egress object for packet sending.

private:

    void processIngress(uint8_t* packet, size_t size); // Method for processing packets

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
    void onArpResolved(const uint8_t* ip, const uint8_t* mac);

    // Member variables
    std::mutex ipInfoMutex;             ///< Mutex for thread-safe access to IP information.

    bool debug;             ///< Flag indicating if debug mode is enabled.

    std::atomic<bool> threadsRunning;   ///< Atomic flag indicating if threads are running.
};

#endif // INTERFACE_H
