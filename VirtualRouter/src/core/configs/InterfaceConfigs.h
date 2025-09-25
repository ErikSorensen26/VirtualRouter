// InterfaceConfigs.h

#ifndef INTERFACE_CONFIGS_H
#define INTERFACE_CONFIGS_H

#include <shared_mutex>
#include <ByteString.hpp>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <map>

class Global;
class TimeManager;
enum class AddressFamily;
enum class InterfaceType;
class MockInterface;
class Internal_NdpTest;
namespace EigrpConfigs
{
    struct InterfaceConfigs;
}
namespace Protocol
{
    class Ndp;
}

/**
 * @class IpInfo
 * @brief Stored IP address and related configuration information.
 */
class InterfaceConfigs
{
public:
    InterfaceConfigs(TimeManager& timeManager, InterfaceType type, float id, const ByteString& mac) : ipv6(timeManager), id(id), interfaceType(type), macAddress(mac) {}

    /**
     * @struct IPv4State
     * @brief Stores IPv4 address and subnet mask information.
     */
    struct IPv4State
    {
        friend class MockInterface;
        friend class Interface;
        std::atomic<uint16_t> mtu{1500}; ///< Maximum Transmission Unit size.
        bool mtuLocal = false;
        ByteString getAddress() { std::shared_lock<std::shared_mutex> lock(ipMutex); return ipAddress; }
        uint8_t getMask() { std::shared_lock<std::shared_mutex> lock(ipMutex); return mask; }
    private:
        std::shared_mutex ipMutex;
        uint8_t mask{0};            ///< Subnet mask.
        ByteString ipAddress{};     ///< IPv4 address.
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
            ByteString ip;
            uint8_t prefix = 0;
            bool tentative{false}, valid{false}, globalTentative{false}, globalValid{false}, deprecated{false};
            uint32_t expirationId = 0, preferredLifetime = 0, preferedExpirationId = 0;

            void validateAddress(bool local = false);
        };

        // Add/Remove functions
        IPv6Address* addAddress(const ByteString& ip, bool local, uint8_t prefix);

        IPv6Address* addUniqueLocalAddress(const ByteString& ip, uint8_t prefixLen);

        void removeAddress(const ByteString& ip, bool local);

        // Validation
        void validateGlobalAddresses();

        void validateLinkLocalAddress();

        // Get IP functions
        ByteString getLocalAddress();

        std::pair<ByteString, uint8_t> getGlobalUnicastPair();
        ByteString getGlobalUnicast();
        uint8_t getGlobalUnicastMask();

        std::pair<ByteString, uint8_t> getLocalUnicastPair();
        ByteString getLocalUnicast();
        uint8_t getLocalUnicastMask();

        std::vector<ByteString> getGlobalList();

        std::vector<ByteString> getLocalList();

        ~IPv6State();

        // Other IPv6 configurations
        std::atomic<uint16_t> mtu{1500}; ///< Maximum Transmission Unit size.

    private:
        IPv6Address* linkLocalAddress = new IPv6Address(); ///< IPv6 address.
        std::vector<IPv6Address*> globalAddresses; ///< Global IPv6 addresses.
        std::vector<IPv6Address*> uniqueLocalAddresses{}; ///< Unique Local Addresses.
    }  ipv6;

    bool hasAddress(const ByteString& address);

    /**
     * @struct Eigrp
     * @brief Stores Eigrp Configs
     */
    struct Eigrp
    {
        std::unordered_set<uint32_t> ipv6AutonomousSystems; ///< Enabled ipv6 autonomous system list
        std::map<std::pair<uint32_t, AddressFamily>, EigrpConfigs::InterfaceConfigs*> eigrpInterfaceConfigList; ///< As number to configuration
    } eigrp;

    ~InterfaceConfigs();
    std::shared_mutex ipMutex;      ///< Mutex for thread-safe access to IP information

    float id;                       ///< Identifier for the interface.
    InterfaceType interfaceType;    ///< Type of interface.
    std::string physicalInterface;
    std::atomic<uint16_t> vlan = 1;              ///< Interface VLAN (defaulted to vlan 1)
    std::atomic<bool> trusted = false;           ///< Identifier for trusted interface.
    std::atomic<uint32_t> bandwidth{1000000};    ///< Bandwidth of the interface in kpbs.
    std::atomic<uint32_t> delay{10};             ///< Delay of the interface in milliseconds.
    std::atomic<uint8_t> ttl{64};                ///< Time To Live.
    std::atomic<uint16_t> globalMtu{1500};

    ByteString getMac();
    void setMac(const ByteString& mac);

private:
    ByteString macAddress{};                     ///< MAC address addociated with the interface.
};

#endif // INTERFACE_CONFIGS_H
