// DhcpInfo.hpp

#ifndef DHCP_INFO_HPP
#define DHCP_INFO_HPP

#include <vector>
#include <IPAddress.h>
#include <random>
#include <atomic>
#include <shared_mutex>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <optional>
#include <ByteUtils.hpp>

namespace interface { class Interface; }

namespace services::dhcp
{
static inline double secondsSinceEpoch()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct ClientID
{
    uint8_t* data = nullptr;
    uint8_t size = 0;
    
    ClientID() = default;

    ClientID(const uint8_t* src, size_t len)
    {
        if (len > 255) throw std::runtime_error("ClientID too large");
        size = static_cast<uint8_t>(len);
        data = static_cast<uint8_t*>(std::malloc(size));
        std::memcpy(data, src, size);
    }

    ~ClientID()
    {
        std::free(data);
    }

    ClientID(const ClientID& other)
    {
        size = other.size;
        data = static_cast<uint8_t*>(std::malloc(size));
        std::memcpy(data, other.data, size);
    }

    ClientID& operator=(const ClientID& other)
    {
        if (this == &other) return *this;
        std::free(data);
        size = other.size;
        data = static_cast<uint8_t*>(std::malloc(size));
        std::memcpy(data, other.data, size);
        return *this;
    }

    bool operator==(const ClientID& other) const
    {
        return size == other.size &&
            std::memcmp(data, other.data, size) == 0;
    }
};

// Initialize a static random generator for transaction IDs
inline static std::mt19937& getTransidGenerator()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

// Generates a random DHCP transaction ID
inline static uint8_t* generateDhcpTransid(uint8_t* out)
{
    static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
    uint32_t transId = dis(getTransidGenerator());
    utils::writeU32(out, transId);
    return out;
}
/**
 * @struct Dhcp
 * @brief Stores DHCP configuration information.
 */
struct DhcpInfo 
{
    std::shared_mutex configMutex;

    types::IPAddress broadcast{};         ///< Broadcast address
    types::IPAddress serverID{};          ///< Server ID
    types::IPAddress router{};            ///< Router address
    std::vector<types::IPAddress> dnsServers{}; ///< List of DNS servers

    std::atomic<std::chrono::steady_clock::time_point> leaseStart;
    std::atomic<uint32_t> leaseTime{};         ///< Lease time for DHCP
    std::atomic<uint32_t> renewalTime{};       ///< Renewal time for DHCP
    std::atomic<uint32_t> rebindingTime{};     ///< Rebinding time for DHCP
    std::atomic<uint8_t> subnetMask{};         ///< Subnet mask

    std::atomic<uint16_t> maxSize = 512;
    std::vector<types::IPAddress> helperAddresses; ///< List of DHCP helper addresses (relay agents)

    std::optional<std::string> authKey = std::nullopt;
    std::string* getAuthKey() { return authKey.has_value() ? &authKey.value() : nullptr; }
    std::atomic<uint64_t> lastReplayCounter = 0;

    std::string hostname;
    std::string domainName;   ///< Domain name provided by the DHCP server

    ClientID clientID;    ///< Client Identifier option (e.g., MAC address or custom ID)

    types::IPAddress requestedIpAddress{};             ///< IP address requested by the client
    types::IPAddress serverIdentifier{};               ///< Server Identifier from the DHCP server

    std::vector<types::IPAddress> ntpServers{};    ///< List of NTP (Network Time Protocol) servers

    std::atomic<uint16_t> mtu{};                     ///< Maximum Transmission Unit (MTU) size
    types::IPAddress tftpServer{};             ///< TFTP server for booting (commonly used in PXE environments)
    std::string bootFile{};             ///< Boot file name (commonly used in PXE environments)

    std::vector<types::IPAddress> staticRoutes{};  ///< List of static routes provided by the DHCP server

    std::atomic<uint32_t> arpTimeout{};                  ///< ARP timeout value (if provided by the DHCP server)

    std::vector<types::IPAddress> winsServers{};    ///< List of WINS servers

    uint8_t vendorSpecificOptions[255];  ///< Vendor-specific options (Option 43 in DHCP)

    types::IPAddress clientIpAddress{};        ///< The client’s IP address (set if the client has already obtained a lease)
    types::IPAddress nextServerIp{};           ///< The next server IP address (used in booting scenarios)
};
} // namespace services

namespace std {
    template <>
    struct hash<services::dhcp::ClientID> {
        size_t operator()(const services::dhcp::ClientID& id) const {
            const uint8_t* data = id.data;
            size_t size = id.size;

            size_t h = 14695981039346656037ull; // FNV offset basis
            for (size_t i = 0; i < size; ++i) {
                h ^= static_cast<size_t>(data[i]);
                h *= 1099511628211ull; // FNV prime
            }

            h ^= size;
            h *= 1099511628211ull;

            return h;
        }
    };
}

#endif //DHCP_INFO_HPP

