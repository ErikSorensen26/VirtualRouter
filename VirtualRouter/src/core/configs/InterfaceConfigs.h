// InterfaceConfigs.h

#ifndef INTERFACE_CONFIGS_H
#define INTERFACE_CONFIGS_H

#include <shared_mutex>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <HeaderHelpers.hpp>
#include <cstring>
#include <map>

// Forward declarations
class Global;
class TimeManager;
class MockInterface;
class Internal_NdpTest;
class IPAddress;
class IPPrefix;
struct HwIfaceInfo;
enum class AddressFamily : uint8_t;
enum class InterfaceType : uint8_t;

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

inline uint32_t calculateInterfaceKey(InterfaceType type, float id)
{
    uint8_t typeEncoded = static_cast<uint8_t>(type);
    float clamped = std::max(0.0f, std::min(id, 65535.256f));
    uint32_t fixed = static_cast<uint32_t>(clamped * 256.0f);
    fixed &= 0x00FFFFFF;
    return (static_cast<uint32_t>(typeEncoded) << 24) | fixed;
}

class InterfaceConfigs
{
public:
    InterfaceConfigs(TimeManager& timeManager, InterfaceType type, float id, const HwIfaceInfo& info);

    ~InterfaceConfigs();

    bool hasAddress(const uint8_t* address);
    bool hasAddress(__uint128_t address);

    uint8_t* getMac(uint8_t* mac);
    uint64_t getMac();
    void setMac(const uint8_t* mac);

    float id;                       ///< Identifier for the interface.
    InterfaceType interfaceType;    ///< Type of interface.
    uint32_t key;                   ///< Interface ID for global identification.

    const HwIfaceInfo& hwInfo;

    std::atomic<uint16_t> vlan = 1;              ///< Interface VLAN (defaulted to vlan 1)
    std::atomic<bool> trusted = false;           ///< Identifier for trusted interface.
    std::atomic<uint32_t> bandwidth{1000000};    ///< Bandwidth of the interface in kpbs.
    std::atomic<uint32_t> delay{10};             ///< Delay of the interface in milliseconds.
    std::atomic<uint8_t> load{1};                ///< Load value of the interface.
    std::atomic<uint8_t> reliability{255};       ///< Reliability of the interface.
    std::atomic<uint8_t> ttl{64};                ///< Time To Live.
    std::atomic<uint16_t> globalMtu{1500};

    std::shared_mutex ipMutex;

    // Sub-configuration structs
    struct IPv4State
    {
        IPv4State() : mask(0), address(0) {}
        std::atomic<uint16_t> mtu{1500};
        std::atomic<bool> mtuLocal{false};

        uint8_t* getAddress(uint8_t* out) const;
        IPAddress getAddress() const;
        uint32_t getAddressInt() const;

        void setAddress(uint32_t newAddress, uint8_t newMask);

        bool compareAddress(const uint8_t* ip);
        bool compareAddress(uint32_t ip);

        uint8_t getMask() const;

    private:
        std::shared_mutex ipMutex;
        std::atomic<uint8_t> mask{0};            ///< Subnet mask.
        std::atomic<uint32_t> address;

        friend class MockInterface;
        friend class Interface;
    } ipv4;

    /**
     * @struct IPv6
     * @brief Stores IPv6 address, subnet mask, and flow label information.
     */
    struct IPv6State
    {
        explicit IPv6State(TimeManager& time);
        ~IPv6State();

        struct IPv6Address
        {
            uint8_t ip[16];
            __uint128_t ipInt;
            uint8_t prefix = 0;

            bool tentative{false};
            bool valid{false};
            bool globalTentative{false};
            bool globalValid{false};
            bool deprecated{false};
            uint32_t expirationId = 0;
            uint32_t preferredLifetime = 0;
            uint32_t preferedExpirationId = 0;

            void validateAddress(bool local = false);

            friend class MockInterface;
            friend class ::Internal_NdpTest;
        };

        // Add/Remove functions
        IPv6Address* addAddress(const uint8_t* ip, bool local, uint8_t prefix);
        IPv6Address* addUniqueLocalAddress(const uint8_t* ip, uint8_t prefixLen);
        IPv6Address* addGlobalAddress(const uint8_t* ip, uint8_t prefixLen);
        void removeLocalAddress();
        void removeAddress(const uint8_t* ip);
        void removeAddress(__uint128_t ip);
        void removeAllAddresses();

        void validateGlobalAddresses();
        void validateLinkLocalAddress();

        uint8_t* getLocalAddress(uint8_t* out) const;
        uint8_t* getGlobalUnicast(uint8_t* out) const;
        uint8_t* getLocalUnicast(uint8_t* out) const;

        __uint128_t getLocalAddress() const;
        __uint128_t getGlobalUnicast() const;
        __uint128_t getLocalUnicast() const;

        bool hasLocalAddress(const uint8_t* addr) const;
        bool hasGlobalUnicast(const uint8_t* addr) const;
        bool hasLocalUnicast(const uint8_t* addr) const;

        bool hasLocalAddress(__uint128_t addr) const;
        bool hasGlobalUnicast(__uint128_t addr) const;
        bool hasLocalUnicast(__uint128_t addr) const;

        uint8_t getGlobalUnicastPair(uint8_t* out) const;
        uint8_t getLocalUnicastPair(uint8_t* out) const;

        uint8_t getGlobalUnicastMask() const;
        uint8_t getLocalUnicastMask() const;

        std::vector<IPAddress> getGlobalList() const;
        std::vector<IPAddress> getLocalList() const;

        std::vector<IPPrefix> getGlobalPrefixList() const;
        std::vector<IPPrefix> getLocalPrefixList() const;


        // Other IPv6 configurations
        std::atomic<uint16_t> mtu{1500}; ///< Maximum Transmission Unit size.
        std::atomic<bool> mtuLocal{false};

    private:
        TimeManager& timeManager;
        mutable std::shared_mutex ipMutex;
        IPv6Address* linkLocalAddress = nullptr;
        std::vector<IPv6Address*> globalAddresses; ///< Global IPv6 addresses.
        std::vector<IPv6Address*> uniqueLocalAddresses{}; ///< Unique Local Addresses.
        void cancelTimers(IPv6Address& addr);

        friend class MockInterface;
        friend class Interface;
        friend class Protocol::Ndp;
        friend class ::Internal_NdpTest;
    }  ipv6;

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

private:
    std::atomic<uint64_t> macAddress;  ///< MAC address associated with the interface.
};

#endif // INTERFACE_CONFIGS_H
