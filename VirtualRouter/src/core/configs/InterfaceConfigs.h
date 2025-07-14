// InterfaceConfigs.h

#include <shared_mutex>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <HeaderHelpers.hpp>
#include <cstring>
#include <map>

class Global;
class TimeManager;
enum class AddressFamily: uint8_t;
class MockInterface;
class Internal_NdpTest;
class IPAddress;
namespace EigrpConfigs
{
    struct InterfaceConfigs;
}
namespace Protocol
{
    namespace Dhcpv6
    {
        struct InterfaceConfigs;
        struct DhcpNetwork;
    }
    class Ndp;
}

#ifndef INTERFACE_CONFIGS_H
#define INTERFACE_CONFIGS_H

/**
 * @enum InterfaceType
 * @brief Enunerates the various types of network interfaces supported
 */
enum class InterfaceType : uint8_t
{
    UNDEFINED = 0,          ///< Undefined interface type.
    ETHERNET = 1,           ///< Ethernet interface type.
    FAST_ETHERNET = 2,      ///< Fast Ethernet type.
    GIGABIT_ETHERNET = 3,   ///< Gigabit Ethernet interface type.
    LOOPBACK = 4,           ///< Loopback interface type.
    PORT_CHANNEL = 5,       ///< Port-channel interface type.
    TUNNEL = 6,             ///< Tunnel interface type.
    VIRTUAL_TEMPLATE = 7,   ///< Virtual Template interface type.
    VLAN = 8                ///< VLAN interface type.
};

inline uint32_t calculateInterfaceKey(InterfaceType type, float id) {
    uint8_t typeEncoded = static_cast<uint8_t>(type);
    uint32_t idEncoded;
    static_assert(sizeof(float) == sizeof(uint32_t), "Unexpected float size");
    std::memcpy(&idEncoded, &id, sizeof(float));
    return (static_cast<uint32_t>(typeEncoded) << 24) | (idEncoded & 0x00FFFFFF);
}

/**
 * @class IpInfo
 * @brief Stored IP address and related configuration information.
 */
class InterfaceConfigs
{
public:
    InterfaceConfigs(TimeManager& timeManager, InterfaceType type, float id, const uint8_t* mac) : ipv6(timeManager), id(id), interfaceType(type), key(calculateInterfaceKey(type, id)) {
        std::memcpy(macAddress, mac, 6);
    }

    /**
     * @struct IPv4State
     * @brief Stores IPv4 address and subnet mask information.
     */
    struct IPv4State
    {
        IPv4State() : mask(0), address(0) {}
        friend class MockInterface;
        friend class Interface;
        std::atomic<uint16_t> mtu{1500}; ///< Maximum Transmission Unit size.
        bool mtuLocal = false;
        uint8_t* getAddress(uint8_t* out) { writeU32(out, address.load(std::memory_order_relaxed)); return out; }
        uint32_t getAddress() { return address.load(std::memory_order_relaxed); }
        void setAddress(const uint8_t* newAddress, uint8_t newMask) { address.store(readU32(newAddress), std::memory_order_release); mask.store(newMask, std::memory_order_relaxed); }
        bool compareAddress(const uint8_t* ip) { return address.load(std::memory_order_relaxed) == readU32(ip); }
        bool compareAddress(uint32_t ip) { return address.load(std::memory_order_release) == ip; }
        uint8_t getMask() { return mask.load(std::memory_order_relaxed); }
    private:
        std::shared_mutex ipMutex;
        std::atomic<uint8_t> mask{0};            ///< Subnet mask.
        std::atomic<uint32_t> address;
    } ipv4;

    /**
     * @struct IPv6
     * @brief Stores IPv6 address, subnet mask, and flow label information.
     */
    struct IPv6State
    {
        IPv6State(TimeManager& time);
        TimeManager& timeManager;
        friend class MockInterface;
        friend class Interface;
        friend class Protocol::Ndp;
        friend class ::Internal_NdpTest;
        std::shared_mutex ipMutex;
        struct IPv6Address
        {
            friend class MockInterface;
            friend class ::Internal_NdpTest;
            uint8_t ip[16];
            uint8_t prefix = 0;
            bool tentative{false}, valid{false}, globalTentative{false}, globalValid{false}, deprecated{false};
            uint32_t expirationId = 0, preferredLifetime = 0, preferedExpirationId = 0;

            void validateAddress(bool local = false);
        };

        // Add/Remove functions
        IPv6Address* addAddress(const uint8_t* ip, bool local, uint8_t prefix);

        IPv6Address* addUniqueLocalAddress(const uint8_t* ip, uint8_t prefixLen);

        void removeAddress(const uint8_t* ip, bool local);

        // Validation
        void validateGlobalAddresses();

        void validateLinkLocalAddress();

        // Get IP functions
        uint8_t* getLocalAddress(uint8_t* out);
        __uint128_t getLocalAddress();

        uint8_t getGlobalUnicastPair(uint8_t* out);
        uint8_t* getGlobalUnicast(uint8_t* out);
        uint8_t getGlobalUnicastMask();
        __uint128_t getGlobalUnicast();

        uint8_t getLocalUnicastPair(uint8_t* out);
        uint8_t* getLocalUnicast(uint8_t* out);
        uint8_t getLocalUnicastMask();
        __uint128_t getLocalUnicast();

        std::vector<IPAddress> getGlobalList();

        std::vector<IPAddress> getLocalList();

        ~IPv6State();

        // Other IPv6 configurations
        std::atomic<uint16_t> mtu{1500}; ///< Maximum Transmission Unit size.

    private:
        IPv6Address* linkLocalAddress = new IPv6Address(); ///< IPv6 address.
        std::vector<IPv6Address*> globalAddresses; ///< Global IPv6 addresses.
        std::vector<IPv6Address*> uniqueLocalAddresses{}; ///< Unique Local Addresses.
    }  ipv6;

    bool hasAddress(const uint8_t* address);

    /**
     * @struct Eigrp
     * @brief Stores Eigrp Configs
     */
    struct Eigrp
    {
        std::unordered_set<uint32_t> ipv6AutonomousSystems; ///< Enabled ipv6 autonomous system list
        std::map<std::pair<uint32_t, AddressFamily>, EigrpConfigs::InterfaceConfigs*> eigrpInterfaceConfigList; ///< As number to configuration
    } eigrp;

    /**
     * @struct Dhcpv6
     * @brief Stores Dhcpv6 Configs
     */
    struct Dhcpv6
    {
        Protocol::Dhcpv6::InterfaceConfigs* configs = nullptr;
        std::vector<Protocol::Dhcpv6::DhcpNetwork*> dhcpNetworks;
    } dhcpv6;

    ~InterfaceConfigs();
    std::shared_mutex ipMutex;      ///< Mutex for thread-safe access to IP information

    float id;                       ///< Identifier for the interface.
    InterfaceType interfaceType;    ///< Type of interface.
    uint32_t key;                   ///< Interface ID for global identification.
    std::atomic<uint16_t> vlan = 1;              ///< Interface VLAN (defaulted to vlan 1)
    std::atomic<bool> trusted = false;           ///< Identifier for trusted interface.
    std::atomic<uint32_t> bandwidth{1000000};    ///< Bandwidth of the interface in kpbs.
    std::atomic<uint32_t> delay{10};             ///< Delay of the interface in milliseconds.
    std::atomic<uint8_t> ttl{64};                ///< Time To Live.
    std::atomic<uint16_t> globalMtu{1500};

    uint8_t* getMac(uint8_t* mac);
    uint64_t getMac();
    void setMac(const uint8_t* mac);

private:
    uint8_t macAddress[6];                     ///< MAC address addociated with the interface.
};

#endif // INTERFACE_CONFIGS_H
