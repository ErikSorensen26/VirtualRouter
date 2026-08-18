/**
 * @file DhcpInfo.hpp
 * @brief Shared DHCP data types used across DHCPv4 and DHCPv6.
 */

/**
 * @defgroup SERVICES Services
 * @brief Network services: DHCP server, relay, and client.
 */

/**
 * @defgroup SERVICES_DHCP DHCP
 * @ingroup SERVICES
 * @brief DHCP service: shared types, DHCPv4, and DHCPv6 implementations.
 */

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

namespace interface { class Interface; } ///< Owns the interfaces DHCP binds to.

/**
 * @namespace services::dhcp
 * @brief Shared DHCP types and helpers used by both the DHCPv4 and DHCPv6 implementations.
 *
 * DhcpInfo holds the per-interface DHCP configuration state common to both address
 * families; ClientID models the variable-length client identifier used to key leases.
 */
namespace services::dhcp
{
static inline double secondsSinceEpoch()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief Variable-length DHCP client identifier (Option 61 / DUID), owning a malloc'd buffer.
 * @ingroup SERVICES_DHCP
 *
 * @warning Copies deep-copy the buffer via malloc/free rather than using RAII containers;
 * callers must not alias @c data across instances.
 */
struct ClientID
{
    uint8_t* data = nullptr;
    uint8_t size = 0;

    ClientID() = default;

    /**
     * @brief Copies @p len bytes from @p src into a newly allocated buffer.
     * @param src Source identifier bytes.
     * @param len Length in bytes; must not exceed 255.
     * @throws std::runtime_error if @p len exceeds the 8-bit size field.
     */
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

/** @brief Returns the process-wide Mersenne Twister used to generate DHCP transaction IDs. */
inline static std::mt19937& getTransidGenerator()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

/**
 * @brief Writes a random 4-byte DHCP transaction ID into @p out.
 * @param out Destination buffer; must have at least 4 bytes available.
 * @return @p out, for chaining.
 */
inline static uint8_t* generateDhcpTransid(uint8_t* out)
{
    static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
    uint32_t transId = dis(getTransidGenerator());
    utils::write<uint32_t>(out, transId);
    return out;
}
/**
 * @brief Per-interface DHCP configuration and lease state, shared by client/server/relay roles.
 * @ingroup SERVICES_DHCP
 *
 * Fields are populated from whichever role (client, server, or relay) is active on the
 * interface. Atomic members can be read or updated without taking @c configMutex; the
 * mutex guards the non-atomic fields (strings, vectors, ClientID).
 */
struct DhcpInfo
{
    std::shared_mutex configMutex; ///< Guards the non-atomic members below.

    types::IPAddress broadcast{};
    types::IPAddress serverID{};
    types::IPAddress router{};
    std::vector<types::IPAddress> dnsServers{};

    std::atomic<std::chrono::steady_clock::time_point> leaseStart;
    std::atomic<uint32_t> leaseTime{};
    std::atomic<uint32_t> renewalTime{};
    std::atomic<uint32_t> rebindingTime{};
    std::atomic<uint8_t> subnetMask{};

    std::atomic<uint16_t> maxSize = 512;
    std::vector<types::IPAddress> helperAddresses; ///< Relay agent addresses to forward client traffic to.

    std::optional<std::string> authKey = std::nullopt;
    std::string* getAuthKey() { return authKey.has_value() ? &authKey.value() : nullptr; }
    std::atomic<uint64_t> lastReplayCounter = 0;

    std::string hostname;
    std::string domainName;

    ClientID clientID;

    types::IPAddress requestedIpAddress{};
    types::IPAddress serverIdentifier{};

    std::vector<types::IPAddress> ntpServers{};

    std::atomic<uint16_t> mtu{};
    types::IPAddress tftpServer{};      ///< PXE boot server address.
    std::string bootFile{};             ///< PXE boot file name.

    std::vector<types::IPAddress> staticRoutes{};

    std::atomic<uint32_t> arpTimeout{}; ///< 0 if not provided by the DHCP server.

    std::vector<types::IPAddress> winsServers{};

    uint8_t vendorSpecificOptions[255]; ///< Raw contents of DHCP Option 43.

    types::IPAddress clientIpAddress{}; ///< Set once the client has obtained a lease.
    types::IPAddress nextServerIp{};    ///< Next server to contact in a PXE boot chain.
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

