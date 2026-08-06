// Dhcpv6AuthManager.cpp

#include "packet/headers/Dhcpv6Header.hpp"
#include "Dhcpv6AuthManager.h"
#include "packet/TlvOptions.hpp"
#include "security/Encryption.hpp"

namespace services::dhcp
{
#pragma region SERVER

void AuthManager::addDelayedKey(uint32_t id, const std::string secret, uint32_t lifetime)
{
    std::lock_guard<std::mutex> lock(mutex);
    delayedKeys[id] = Key{ secret, std::chrono::steady_clock::now(), std::chrono::seconds(lifetime) };
}

std::optional<AuthManager::Key> AuthManager::getDelayedKey(uint64_t keyID)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = delayedKeys.find(keyID);
    if (it == delayedKeys.end() || it->second.isExpired()) return std::nullopt;
    return it->second;
}

std::optional<AuthManager::Key> AuthManager::getKeyForClient(const ClientID& duid)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = clientToKey.find(duid);
    if (it == clientToKey.end()) return std::nullopt;
    return getDelayedKey(it->second);
}

uint64_t AuthManager::getNextCounter()
{
    return globalCounter.fetch_add(1, std::memory_order_relaxed);
}

bool AuthManager::isReplayValid(const ClientID& duid, uint64_t val)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = replayCounter.find(duid);
    if (it == replayCounter.end()) return true;
    return val > it->second;
}

void AuthManager::updateReplayCounter(const ClientID& duid, uint64_t val)
{
    std::lock_guard<std::mutex> lock(mutex);
    replayCounter[duid] = val;
}

void AuthManager::clearExpiredKeys()
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = delayedKeys.begin(); it != delayedKeys.end(); )
    {
        if (it->second.isExpired()) it = delayedKeys.erase(it);
        else ++it;
    }
}

bool AuthManager::hasValidKey(AuthProtocol proto)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (proto == AuthProtocol::DELAYED)
    {
        for (auto& [_, key] : delayedKeys)
            if (!key.isExpired()) return true;
    }
    return false;
}

std::optional<AuthManager::DelayedAuthInfo> AuthManager::addDelayedAuthOption(packet::TLV16BufferManager& tlv, const ClientID& duid)
{
    if (!config.delayedEnabled.load(std::memory_order_relaxed))
        return std::nullopt; // Disabled

    auto keyOpt = getKeyForClient(duid);
    if (!keyOpt) return {}; // Invalid

    const Key& key = *keyOpt;
    AuthAlgorithm algo = config.algorithm.load(std::memory_order_relaxed);
    AuthRDM rdm = config.rdm.load(std::memory_order_relaxed);

    uint64_t replay = (rdm == AuthRDM::TIMESTAMP)
        ? static_cast<uint64_t>(secondsSinceEpoch())
        : getNextCounter();

    updateReplayCounter(duid, replay);

    const size_t headerSize = 15 + (algo == AuthAlgorithm::HMACMD5 ? security::authentication::MD5_DIGEST_LENGTH : security::authentication::SHA_DIGEST_LENGTH);

    uint8_t* option = tlv.getNextValBuf(headerSize);
    if (!option) return {};

    option[0] = static_cast<uint8_t>(AuthProtocol::DELAYED);
    option[1] = static_cast<uint8_t>(algo);
    option[2] = static_cast<uint8_t>(rdm);

    utils::writeU64(option + 3, replay);
    utils::writeU32(option + 11, clientToKey[duid]);

    // Zero out auth digest for digest calculation
    std::memset(option + 15, 0, headerSize - 15);

    tlv.append(DHCPV6_OPTION_AUTHENTICATION, headerSize, nullptr, headerSize);

    // Return info for calculating digest later
    return DelayedAuthInfo{
        .digest = option + 15,
        .key = key.secret,
        .algo = algo
    };
}

void AuthManager::addDelayedAuthDigest(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv, DelayedAuthInfo& auth)
{
    if (auth.algo == AuthAlgorithm::HMACMD5)
        security::authentication::generateHMAC(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size(), security::authentication::HmacType::MD5);
    else
        security::authentication::generateHMAC(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size(), security::authentication::HmacType::SHA1);
}

bool AuthManager::validateDelayedAuth(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt, const ClientID& duid)
{
    if (!config.delayedEnabled.load(std::memory_order_relaxed))
        return true; // Valid, auth not required
    if (!opt) return false;

    const uint8_t* buf = opt->value;
    AuthProtocol proto = static_cast<AuthProtocol>(buf[0]);
    AuthAlgorithm algo = static_cast<AuthAlgorithm>(buf[1]);
    AuthRDM rdm = static_cast<AuthRDM>(buf[2]);

    if (proto != AuthProtocol::DELAYED || rdm != config.rdm.load(std::memory_order_relaxed))
        return false;

    uint64_t replay = utils::readU64(buf + 3);
    uint64_t keyID = utils::readU64(buf + 11);
    uint8_t mac[20];
    size_t macLen = opt->valueSize - 15;
    std::memcpy(mac, opt->value + 15, macLen);
    std::memset(const_cast<uint8_t*>(opt->value) + 15, 0, macLen); // Zero digest for validation

    if (macLen != (algo == AuthAlgorithm::HMACMD5 ? security::authentication::MD5_DIGEST_LENGTH : security::authentication::SHA_DIGEST_LENGTH) || !isReplayValid(duid, replay))
        return false;
    updateReplayCounter(duid, replay);

    auto keyOpt = getDelayedKey(keyID);
    if (!keyOpt) return false;

    const Key& key = *keyOpt;
    size_t dhcpSize = static_cast<size_t>(opt->value + opt->valueSize - dhcp.buffer);
    if (algo == AuthAlgorithm::HMACMD5)
        security::authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.secret.data()), key.secret.size(), security::authentication::HmacType::MD5);
    else
        security::authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.secret.data()), key.secret.size(), security::authentication::HmacType::SHA1);

    return std::memcmp(mac, opt->value + 15, macLen) == 0;
}

std::optional<__uint128_t> AuthManager::addRkapAuthOption(packet::TLV16BufferManager& tlv, const ClientID& duid)
{
    if (!config.rkapEnabled.load(std::memory_order_relaxed))
        return std::nullopt;

    uint8_t* option = tlv.getNextValBuf(20);
    if (!option) return {};

    option[0] = static_cast<uint8_t>(AuthProtocol::RKAP);
    option[1] = static_cast<uint8_t>(AuthAlgorithm::HMACMD5);
    option[2] = 0; // No replay
    option[3] = 1; // Key

    // Generate key
    std::random_device rd;
    std::mt19937 gen(rd());
    uint64_t high = gen();
    uint64_t low = gen();
    __uint128_t key = (static_cast<__uint128_t>(high) << 64) | low;

    utils::writeU128(option + 4, key);

    tlv.append(DHCPV6_OPTION_AUTHENTICATION, 20, nullptr, 20);

    return key;
}

bool AuthManager::validateRkapDigest(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt, const ClientID& duid, __uint128_t secret)
{
    if (!config.rkapEnabled.load(std::memory_order_relaxed))
        return true; // Valid, auth is not required
    if (!opt || opt->valueSize != 20)
        return false;

    const uint8_t* buf = opt->value;
    AuthProtocol proto = static_cast<AuthProtocol>(buf[0]);
    AuthAlgorithm algo = static_cast<AuthAlgorithm>(buf[1]);
    uint8_t rdm = buf[2];
    uint8_t type = buf[3];

    if (proto != AuthProtocol::RKAP || algo != AuthAlgorithm::HMACMD5 || rdm != 0 || type != 2)
        return false;

    // Validate endian
    uint8_t key[16];
    utils::writeU128(key, secret);

    // Harvest digest
    uint8_t digest[16];
    std::memcpy(digest, opt->value + 4, 16);
    std::memset(const_cast<uint8_t*>(opt->value) + 4, 0, 16);

    size_t dhcpSize = static_cast<size_t>(opt->value + opt->valueSize - dhcp.buffer);
    security::authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 4, dhcp.buffer, dhcpSize, key, 16, security::authentication::HmacType::MD5);

    return std::memcmp(digest, opt->value + 4, 16) == 0;
}

#pragma endregion
#pragma region CLIENT

void ClientAuthManager::addDelayedKey(uint32_t id, const std::string secret)
{
    std::lock_guard<std::mutex> lock(mutex);
    delayedKeys.push_back({ id, secret });
}

std::optional<std::pair<uint32_t, std::string>> ClientAuthManager::getDelayedKey(size_t index)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (delayedKeys.size() > index + 1) return std::nullopt;
    return delayedKeys.at(index);
}

uint64_t ClientAuthManager::getNextCounter()
{
    return replayCounter.fetch_add(1, std::memory_order_relaxed);
}

bool ClientAuthManager::isReplayValid(uint64_t val)
{
    return val > replayCounter.load(std::memory_order_relaxed);
}

void ClientAuthManager::updateReplayCounter(uint64_t val)
{
    replayCounter.store(val, std::memory_order_release);
}

std::optional<ClientAuthManager::DelayedAuthInfo> ClientAuthManager::addDelayedAuthOption(packet::TLV16BufferManager& tlv)
{
    if (!config.delayedEnabled.load(std::memory_order_relaxed))
        return std::nullopt; // Disabled

    auto keyOpt = getDelayedKey(currentKeyIndex.load(std::memory_order_relaxed));
    if (!keyOpt) return {}; // Invalid

    const auto& key = *keyOpt;
    AuthAlgorithm algo = config.algorithm.load(std::memory_order_relaxed);
    AuthRDM rdm = config.rdm.load(std::memory_order_relaxed);

    uint64_t replay = (rdm == AuthRDM::TIMESTAMP)
        ? static_cast<uint64_t>(secondsSinceEpoch())
        : getNextCounter();

    updateReplayCounter(replay);

    const size_t headerSize = 15 + (algo == AuthAlgorithm::HMACMD5 ? security::authentication::MD5_DIGEST_LENGTH : security::authentication::SHA_DIGEST_LENGTH);

    uint8_t* option = tlv.getNextValBuf(headerSize);
    if (!option) return {};

    option[0] = static_cast<uint8_t>(AuthProtocol::DELAYED);
    option[1] = static_cast<uint8_t>(algo);
    option[2] = static_cast<uint8_t>(rdm);

    utils::writeU64(option + 3, replay);
    utils::writeU32(option + 11, key.first);

    // Zero out auth digest for digest calculation
    std::memset(option + 15, 0, headerSize - 15);

    tlv.append(DHCPV6_OPTION_AUTHENTICATION, headerSize, nullptr, headerSize);

    // Return info for calculating digest later
    return DelayedAuthInfo{
        .digest = option + 15,
        .key = key.second,
        .algo = algo
    };
}

void ClientAuthManager::addDelayedAuthDigest(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv, DelayedAuthInfo& auth)
{
    if (auth.algo == AuthAlgorithm::HMACMD5)
        security::authentication::generateHMAC(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size(), security::authentication::HmacType::MD5);
    else
        security::authentication::generateHMAC(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size(), security::authentication::HmacType::SHA1);
}

bool ClientAuthManager::validateDelayedAuth(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt)
{
    if (!config.delayedEnabled.load(std::memory_order_relaxed))
        return true; // Valid, auth not required
    if (!opt) return false;

    const uint8_t* buf = opt->value;
    AuthProtocol proto = static_cast<AuthProtocol>(buf[0]);
    AuthAlgorithm algo = static_cast<AuthAlgorithm>(buf[1]);
    AuthRDM rdm = static_cast<AuthRDM>(buf[2]);

    if (proto != AuthProtocol::DELAYED || rdm != config.rdm.load(std::memory_order_relaxed))
        return false;

    uint64_t replay = utils::readU64(buf + 3);
    uint64_t keyID = utils::readU64(buf + 11);
    uint8_t mac[20];
    size_t macLen = opt->valueSize - 15;
    std::memcpy(mac, opt->value + 15, macLen);
    std::memset(const_cast<uint8_t*>(opt->value) + 15, 0, macLen); // Zero digest for validation

    if (macLen != (algo == AuthAlgorithm::HMACMD5 ? security::authentication::MD5_DIGEST_LENGTH : security::authentication::SHA_DIGEST_LENGTH) || !isReplayValid(replay))
        return false;
    updateReplayCounter(replay);

    auto keyOpt = getDelayedKey(keyID);
    if (!keyOpt) return false;

    const auto& key = *keyOpt;
    size_t dhcpSize = static_cast<size_t>(opt->value + opt->valueSize - dhcp.buffer);
    if (algo == AuthAlgorithm::HMACMD5)
        security::authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.second.data()), key.second.size(), security::authentication::HmacType::MD5);
    else
        security::authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.second.data()), key.second.size(), security::authentication::HmacType::SHA1);

    return std::memcmp(mac, opt->value + 15, macLen) == 0;
}

const uint8_t* ClientAuthManager::addRkapAuthOption(packet::TLV16BufferManager& tlv, const uint8_t* key)
{
    if (!config.rkapEnabled.load(std::memory_order_relaxed))
        return nullptr;

    uint8_t* option = tlv.getNextValBuf(20);
    if (!option) return {};

    option[0] = static_cast<uint8_t>(AuthProtocol::RKAP);
    option[1] = static_cast<uint8_t>(AuthAlgorithm::HMACMD5);
    option[2] = 0; // No replay
    option[3] = 1; // Key

    std::memset(option + 4, 0, 16);

    tlv.append(DHCPV6_OPTION_AUTHENTICATION, 20, nullptr, 20);

    return option + 4;
}

void ClientAuthManager::addRkapAuthDigest(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv, const uint8_t* key, uint8_t* digest)
{
    security::authentication::generateHMAC(digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), key, 16, security::authentication::HmacType::MD5);
}

uint8_t* ClientAuthManager::validateRkapDigest(packet::Dhcpv6Header& dhcp, packet::TLV16Option* opt)
{
    if (!config.rkapEnabled.load(std::memory_order_relaxed))
        return nullptr; // Valid, auth is not required
    if (!opt || opt->valueSize != 20)
        return nullptr;

    const uint8_t* buf = opt->value;
    AuthProtocol proto = static_cast<AuthProtocol>(buf[0]);
    AuthAlgorithm algo = static_cast<AuthAlgorithm>(buf[1]);
    uint8_t rdm = buf[2];
    uint8_t type = buf[3];

    if (proto != AuthProtocol::RKAP || algo != AuthAlgorithm::HMACMD5 || rdm != 0 || type != 2)
        return nullptr;

    // Harvest key
    return const_cast<uint8_t*>(opt->value + 4);
}

#pragma endregion

} // namespace services::dhcp
