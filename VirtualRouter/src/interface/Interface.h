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
    void removeAllIPv6();
    void removeIPv6(const uint8_t* ip = nullptr);

    std::vector<std::array<uint8_t, 16>> getTentativeAddress();
    void markAddressDuplicate(const uint8_t* address, bool linkLocal = false);

    virtual void shutdown(bool shut);
    void physicalShutdown(bool carrier);
    virtual void enqueuePacket(PacketBuilder& packetInfo, const uint8_t* mac = nullptr);

    VirtualRouter* getVRF();
    bool setVRF(VirtualRouter* vrf);

    std::atomic<bool> shutdownFlag = true; ///< Flag indicating if the interface is in shutdown state.
    std::atomic<bool> carrierFlag = true; ///< Flag indicating if carrier is enabled.

    // Member Variables
    InterfaceConfigs configs;         ///< IP configuration information.

    Protocol::Arp* arp = nullptr;     ///< ARP protocol handler.
    Protocol::Ndp* ndp = nullptr;     ///< NDP protocol handler.

    // L4 Protocols
    std::map<uint32_t, Eigrp::EigrpInterfaceInstance> eigrpInterfaceList; ///< EIGRP interface instance.
    EigrpConfigs::InterfaceConfigs* getEigrpConfig(uint32_t as, AddressFamily af, bool negate);

    // L5 Protocols
    Protocol::DhcpClient* dhcp = nullptr;     ///< DHCP Client protocol handler.
    //Protocol::Dhcpv6Client* dhcpv6 = nullptr; ///< Dhcpv6 client protocol handler.

    void stopThreads();
    virtual void startThreads();
    TxDistributor* tx;      ///< Egress object for packet sending.

    void processIngress(uint8_t* packet, size_t size); // Method for processing packets

private:

    std::atomic<VirtualRouter*> routingInstance = nullptr;

    void stateChange(StateChange state);
    void stateChangeV6(StateChange state);

    // Member variables
    std::mutex ipInfoMutex;             ///< Mutex for thread-safe access to IP information.

    bool debug;             ///< Flag indicating if debug mode is enabled.

    std::atomic<bool> threadsRunning;   ///< Atomic flag indicating if threads are running.
};

#endif // INTERFACE_H
