/**
 * @file Dhcpv6AuthManager.h
 * @brief DHCPv6 authentication key storage and Delayed/RKAP digest handling.
 */

// Dhcpv6AuthManager.h

#ifndef DHCPV6_AUTH_MANAGER
#define DHCPV6_AUTH_MANAGER

#include <string>
#include <chrono>
#include <atomic>
#include <unordered_map>

#include "dhcp/DhcpInfo.hpp"

namespace packet { struct Dhcpv6Header; struct TLV16BufferManager; struct TLV16Option; }

namespace services::dhcp
{
enum class AuthProtocol : uint8_t { DELAYED = 2, RKAP = 3 };
enum class AuthAlgorithm : uint8_t { HMACMD5 = 1, HMACSHA1 = 2 };
enum class AuthRDM : uint8_t { MONO = 0, TIMESTAMP = 1, };

struct AuthSettings
{
    std::atomic<AuthAlgorithm> algorithm = AuthAlgorithm::HMACMD5;
    std::atomic<AuthRDM> rdm = AuthRDM::MONO;
    std::atomic<bool> delayedEnabled = false;
    std::atomic<bool> rkapEnabled = false;
};

/**
 * @class AuthManager
 * @brief Holds the authentication keys and settings used for DHCPv6 messages.
 * @ingroup SERVICES_DHCP_V6
 */
class AuthManager
{
public:

    /**
     * @brief Authentication secret plus the timestamp and TTL used to expire it.
     * @ingroup SERVICES_DHCP_V6
     */
    struct Key
    {
        std::string secret;
        std::chrono::steady_clock::time_point created;
        std::chrono::seconds lifetime;

        bool isExpired() const 
        {
            return std::chrono::steady_clock::now() - created > lifetime;
        }
    };

private:
    std::mutex mutex;

    std::unordered_map<uint64_t, Key> delayedKeys;
    std::unordered_map<ClientID, uint64_t> clientToKey; // Duid -> keyID
    std::unordered_map<ClientID, uint64_t> replayCounter; // Per-client DUID -> replay counter

    std::unordered_map<ClientID, __uint128_t> rkapSecrets;

    std::atomic<uint64_t> globalCounter = 1; // Used when sending replies
    AuthSettings config;

public:
    AuthSettings& getSettings() { return config; }

    struct DelayedAuthInfo
    {
        uint8_t* digest = nullptr;
        std::string key;
        AuthAlgorithm algo;
    };

    // Delayed
    void addDelayedKey(uint32_t keyID, const std::string secret, uint32_t lifetime);
    std::optional<Key> getDelayedKey(uint64_t keyID);
    std::optional<Key> getKeyForClient(const ClientID& duid);
    
    std::optional<DelayedAuthInfo> addDelayedAuthOption(packet::TLV16BufferManager& tlv, const ClientID& duid);
    void addDelayedAuthDigest(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv, DelayedAuthInfo& auth);
    bool validateDelayedAuth(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt, const ClientID& duid);

    // RKAP
    std::optional<__uint128_t> addRkapAuthOption(packet::TLV16BufferManager& tlv, const ClientID& duid);
    bool validateRkapDigest(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt, const ClientID& duid, __uint128_t rkapKey);

    // Utils
    bool isReplayValid(const ClientID& duid, uint64_t counter);
    void updateReplayCounter(const ClientID& duid, uint64_t counter);
    uint64_t getNextCounter();
    void clearExpiredKeys();
    bool hasValidKey(AuthProtocol proto);
};

/**
 * @brief Client-side counterpart to AuthManager: holds Delayed/RKAP keys for a single client.
 * @ingroup SERVICES_DHCP_V6
 */
class ClientAuthManager
{
private:
    std::mutex mutex;

    std::vector<std::pair<uint32_t, std::string>> delayedKeys;
    std::atomic<uint64_t> replayCounter = 0;
public:

    std::atomic<size_t> currentKeyIndex = 0;

    AuthSettings config;

    AuthSettings& getSettings() { return config; }

    struct DelayedAuthInfo
    {
        uint8_t* digest = nullptr;
        std::string key;
        AuthAlgorithm algo;
    };

    // Delayed
    void addDelayedKey(uint32_t keyID, const std::string secret);
    std::optional<std::pair<uint32_t, std::string>> getDelayedKey(size_t index);
    
    std::optional<DelayedAuthInfo> addDelayedAuthOption(packet::TLV16BufferManager& tlv);
    void addDelayedAuthDigest(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv, DelayedAuthInfo& auth);
    bool validateDelayedAuth(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt);

    // RKAP
    const uint8_t* addRkapAuthOption(packet::TLV16BufferManager& tlv, const uint8_t* key);
    void addRkapAuthDigest(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv, const uint8_t* key, uint8_t* digest);
    uint8_t* validateRkapDigest(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt);

    // Utils
    bool isReplayValid(uint64_t counter);
    void updateReplayCounter(uint64_t counter);
    uint64_t getNextCounter();
    bool hasValidKey(AuthProtocol proto);
};
} // namespace services::dhcp

#endif // DHCPV6_AUTH_MANAGER

