// InterfaceConfigs.h

#ifndef INTERFACE_CONFIGS_H
#define INTERFACE_CONFIGS_H

#include <shared_mutex>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <map>
#include <unordered_set>
#include <IPAddress.h>
#include <optional>

#include "packet/HeaderHelpers.hpp"
#include "configs/registry/router/OspfInterfaceRegistry.h"

// Forward declarations
class Global;
class TimeManager;
class MockInterface;
class Internal_NdpTest;
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

    bool hasAddress(const uint8_t* address, uint8_t len);
    bool hasAddress(__uint128_t address, uint8_t len);

    uint8_t* getMac(uint8_t* mac);
    uint64_t getMac();
    void setMac(const uint8_t* mac);

    float id;                       ///< Identifier for the interface.
    InterfaceType interfaceType;    ///< Type of interface.
    uint32_t key;                   ///< Interface ID for global identification.

    const HwIfaceInfo& hwInfo;

    std::atomic<uint8_t> tid = 0;        ///< Topology ID (default to tid 0)
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

        void setPrimaryAddress(IPv4Prefix prefix);
        void addSecondaryAddress(IPv4Prefix prefix);

        void removePrimaryAddress();
        void removeSecondaryAddress(IPv4Prefix prefix);
        
        uint8_t* getPrimaryAddress(uint8_t* out) const;
        uint8_t* getSecondaryAddress(uint8_t* out) const;

        IPv4Address getPrimaryAddress() const;
        std::optional<IPv4Address> getSecondaryAddress() const;

        bool hasPrimaryAddress() const;
        bool hasPrimaryAddress(IPv4Prefix prefix) const;
        bool hasPrimaryAddress(const uint8_t* addr, uint8_t mask) const;
        bool hasSecondaryAddress(IPv4Prefix prefix) const;
        bool hasSecondaryAddress(const uint8_t* addr, uint8_t mask) const;

        uint8_t getPrimaryPrefix(uint8_t* out) const;
        std::optional<uint8_t> getSecondaryPrefix(uint8_t* out) const;

        IPv4Prefix getPrimaryPrefix() const;
        std::optional<IPv4Prefix> getSecondaryPrefix();

        uint8_t getPrimaryMask() const;
        std::optional<uint8_t> getSecondaryMask() const;

        std::vector<IPv4Address> getSecondaryList() const;
        std::vector<IPv4Prefix> getSecondaryPrefixList(bool maintainAddress = false) const;

        std::unordered_set<IPv4Address> getSecondarySet() const;
        std::unordered_set<IPv4Prefix> getSecondaryPrefixSet(bool maintainAddress = false) const;

        bool comparePrimaryAddress(const uint8_t* ip);
        bool comparePrimaryAddress(IPv4Address ip);

    private:
        mutable std::mutex ipMutex;
        std::atomic<uint8_t> mask{0};            ///< Subnet mask.
        std::atomic<uint32_t> address;

        std::vector<IPv4Prefix> secondary;

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
            ::IPv6Address addr;
            uint8_t length;

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

        bool hasRoutableAddress();

        // Add/Remove functions
        IPv6Address* addAddress(const IPv6Prefix& ip, bool local);
        IPv6Address* addUniqueLocalAddress(const IPv6Prefix& ip);
        IPv6Address* addGlobalAddress(const IPv6Prefix& ip);
        void removeLocalAddress();
        void removeAddress(const IPv6Prefix& prefix);
        void removeAllAddresses();

        void validateGlobalAddresses();
        void validateLinkLocalAddress();

        uint8_t* getLocalAddress(uint8_t* out) const;
        uint8_t* getGlobalUnicast(uint8_t* out) const;
        uint8_t* getLocalUnicast(uint8_t* out) const;

        ::IPv6Address getLocalAddress() const;
        ::IPv6Address getGlobalUnicast() const;
        ::IPv6Address getLocalUnicast() const;

        bool hasAddress(const uint8_t* addr);
        bool hasAddress(::IPv6Address addr);

        bool hasLocalAddress(const uint8_t* addr, uint8_t len) const;
        bool hasGlobalUnicast(const uint8_t* addr, uint8_t len) const;
        bool hasLocalUnicast(const uint8_t* addr, uint8_t len) const;

        bool hasLocalAddress(const IPv6Prefix& prefix) const;
        bool hasGlobalUnicast(const IPv6Prefix& prefix) const;
        bool hasLocalUnicast(const IPv6Prefix& prefix) const;

        uint8_t getLocalPrefix(uint8_t* out) const;
        uint8_t getGlobalUnicastPrefix(uint8_t* out) const;
        uint8_t getLocalUnicastPrefix(uint8_t* out) const;

        ::IPv6Prefix getLocalPrefix() const;
        ::IPv6Prefix getGlobalUnicastPrefix() const;
        ::IPv6Prefix getLocalUnicastPrefix() const;

        uint8_t getLocalMask() const;
        uint8_t getGlobalUnicastMask() const;
        uint8_t getLocalUnicastMask() const;

        std::vector<::IPv6Address> getRoutableList() const;
        std::vector<::IPv6Address> getGlobalList() const;
        std::vector<::IPv6Address> getLocalList() const;

        std::vector<::IPv6Prefix> getRoutablePrefixList(bool maintainAddress = false) const;
        std::vector<::IPv6Prefix> getGlobalPrefixList(bool maintainAddress = false) const;
        std::vector<::IPv6Prefix> getLocalPrefixList(bool maintainAddress = false) const;

        std::unordered_set<::IPv6Address> getRoutableSet() const;
        std::unordered_set<::IPv6Address> getGlobalSet() const;
        std::unordered_set<::IPv6Address> getUniqueSet() const;

        std::unordered_set<::IPv6Prefix> getRoutablePrefixSet(bool maintainAddress = false) const;
        std::unordered_set<::IPv6Prefix> getGlobalPrefixSet(bool maintainAddress = false) const;
        std::unordered_set<::IPv6Prefix> getUniquePrefixSet(bool maintainAddress = false) const;


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
        std::map<std::pair<uint32_t, AddressFamily>, EigrpConfigs::InterfaceConfigs> eigrpInterfaceConfigList; ///< As number to configuration
    } eigrp;

    /**
     * @struct Ospf
     * @brief Stores Ospf configs
     */
    struct Ospf
    {
        std::unordered_map<uint32_t, uint32_t> enabledProcesses;
        std::optional<Config::Reference<Config::OspfInterfaceBaseRegistry>> ospfInterfaceConfigs = std::nullopt;
        std::map<std::pair<uint32_t, AddressFamily>, Config::Reference<Config::OspfInterfaceAddressFamilyRegistry>> ospfInterfaceConfigList;
    } ospf;

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
