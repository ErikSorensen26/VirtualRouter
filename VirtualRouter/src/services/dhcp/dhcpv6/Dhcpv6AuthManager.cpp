#include <Dhcpv6AuthManager.h>
#include <PacketStructure.h>
#include <TLVOptions.hpp>
#include <Encryption.hpp>
#include <Functions.h>

using namespace std::chrono;

#pragma region SERVER

void Protocol::Dhcpv6::AuthManager::addDelayedKey(uint32_t id, const std::string secret, uint32_t lifetime)
{
    std::lock_guard<std::mutex> lock(mutex);
    delayedKeys[id] = Key{ secret, steady_clock::now(), seconds(lifetime) };
}

std::optional<Protocol::Dhcpv6::AuthManager::Key> Protocol::Dhcpv6::AuthManager::getDelayedKey(uint64_t keyID)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = delayedKeys.find(keyID);
    if (it == delayedKeys.end() || it->second.isExpired()) return std::nullopt;
    return it->second;
}

std::optional<Protocol::Dhcpv6::AuthManager::Key> Protocol::Dhcpv6::AuthManager::getKeyForClient(const ClientID& duid)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = clientToKey.find(duid);
    if (it == clientToKey.end()) return std::nullopt;
    return getDelayedKey(it->second);
}

uint64_t Protocol::Dhcpv6::AuthManager::getNextCounter()
{
    return globalCounter.fetch_add(1, std::memory_order_relaxed);
}

bool Protocol::Dhcpv6::AuthManager::isReplayValid(const ClientID& duid, uint64_t val)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = replayCounter.find(duid);
    if (it == replayCounter.end()) return true;
    return val > it->second;
}

void Protocol::Dhcpv6::AuthManager::updateReplayCounter(const ClientID& duid, uint64_t val)
{
    std::lock_guard<std::mutex> lock(mutex);
    replayCounter[duid] = val;
}

void Protocol::Dhcpv6::AuthManager::clearExpiredKeys()
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = delayedKeys.begin(); it != delayedKeys.end(); )
    {
        if (it->second.isExpired()) it = delayedKeys.erase(it);
        else ++it;
    }
}

bool Protocol::Dhcpv6::AuthManager::hasValidKey(AuthProtocol proto)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (proto == AuthProtocol::DELAYED)
    {
        for (auto& [_, key] : delayedKeys)
            if (!key.isExpired()) return true;
    }
    return false;
}

std::optional<Protocol::Dhcpv6::AuthManager::DelayedAuthInfo> Protocol::Dhcpv6::AuthManager::addDelayedAuthOption(TLV16BufferManager& tlv, const ClientID& duid)
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

    const size_t headerSize = 15 + (algo == AuthAlgorithm::HMACMD5 ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH);

    uint8_t* option = tlv.getNextValBuf(headerSize);
    if (!option) return {};

    option[0] = static_cast<uint8_t>(AuthProtocol::DELAYED);
    option[1] = static_cast<uint8_t>(algo);
    option[2] = static_cast<uint8_t>(rdm);

    writeU64(option + 3, replay);
    writeU32(option + 11, clientToKey[duid]);

    // Zero out auth digest for digest calculation
    std::memset(option + 15, 0, headerSize - 15);

    tlv.append(Variable::Dhcp::Option::authentication, headerSize, nullptr, headerSize);

    // Return info for calculating digest later
    return DelayedAuthInfo{
        .digest = option + 15,
        .key = key.secret,
        .algo = algo
    };
}

void Protocol::Dhcpv6::AuthManager::addDelayedAuthDigest(Dhcpv6Header& dhcp, TLV16BufferManager& tlv, DelayedAuthInfo& auth)
{
    if (auth.algo == AuthAlgorithm::HMACMD5)
        Authentication::generateMD5(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size());
    else
        Authentication::generateHMAC(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size(), Authentication::SHA::SHA1);
}

bool Protocol::Dhcpv6::AuthManager::validateDelayedAuth(Dhcpv6Header& dhcp, TLV16Option* opt, const ClientID& duid)
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

    uint64_t replay = readU64(buf + 3);
    uint64_t keyID = readU64(buf + 11);
    uint8_t mac[20];
    size_t macLen = opt->valueSize - 15;
    std::memcpy(mac, opt->value + 15, macLen);
    std::memset(const_cast<uint8_t*>(opt->value) + 15, 0, macLen); // Zero digest for validation

    if (macLen != (algo == AuthAlgorithm::HMACMD5 ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH) || !isReplayValid(duid, replay))
        return false;
    updateReplayCounter(duid, replay);

    auto keyOpt = getDelayedKey(keyID);
    if (!keyOpt) return false;

    const Key& key = *keyOpt;
    size_t dhcpSize = static_cast<size_t>(opt->value + opt->valueSize - dhcp.buffer);
    if (algo == AuthAlgorithm::HMACMD5)
        Authentication::generateMD5(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.secret.data()), key.secret.size());
    else
        Authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.secret.data()), key.secret.size(), Authentication::SHA::SHA1);

    return std::memcmp(mac, opt->value + 15, macLen) == 0;
}

std::optional<__uint128_t> Protocol::Dhcpv6::AuthManager::addRkapAuthOption(TLV16BufferManager& tlv, const ClientID& duid)
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

    writeU128(option + 4, key);

    tlv.append(Variable::Dhcpv6::Options::auth, 20, nullptr, 20);

    return key;
}

bool Protocol::Dhcpv6::AuthManager::validateRkapDigest(Dhcpv6Header& dhcp, TLV16Option* opt, const ClientID& duid, __uint128_t secret)
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
    writeU128(key, secret);

    // Harvest digest
    uint8_t digest[16];
    std::memcpy(digest, opt->value + 4, 16);
    std::memset(const_cast<uint8_t*>(opt->value) + 4, 0, 16);

    size_t dhcpSize = static_cast<size_t>(opt->value + opt->valueSize - dhcp.buffer);
    Authentication::generateMD5(const_cast<uint8_t*>(opt->value) + 4, dhcp.buffer, dhcpSize, key, 16);

    return std::memcmp(digest, opt->value + 4, 16) == 0;
}

#pragma endregion
#pragma region CLIENT

void Protocol::Dhcpv6::ClientAuthManager::addDelayedKey(uint32_t id, const std::string secret)
{
    std::lock_guard<std::mutex> lock(mutex);
    delayedKeys.push_back({ id, secret });
}

std::optional<std::pair<uint32_t, std::string>> Protocol::Dhcpv6::ClientAuthManager::getDelayedKey(size_t index)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (delayedKeys.size() > index + 1) return std::nullopt;
    return delayedKeys.at(index);
}

uint64_t Protocol::Dhcpv6::ClientAuthManager::getNextCounter()
{
    return replayCounter.fetch_add(1, std::memory_order_relaxed);
}

bool Protocol::Dhcpv6::ClientAuthManager::isReplayValid(uint64_t val)
{
    return val > replayCounter.load(std::memory_order_relaxed);
}

void Protocol::Dhcpv6::ClientAuthManager::updateReplayCounter(uint64_t val)
{
    replayCounter.store(val, std::memory_order_release);
}

std::optional<Protocol::Dhcpv6::ClientAuthManager::DelayedAuthInfo> Protocol::Dhcpv6::ClientAuthManager::addDelayedAuthOption(TLV16BufferManager& tlv)
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

    const size_t headerSize = 15 + (algo == AuthAlgorithm::HMACMD5 ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH);

    uint8_t* option = tlv.getNextValBuf(headerSize);
    if (!option) return {};

    option[0] = static_cast<uint8_t>(AuthProtocol::DELAYED);
    option[1] = static_cast<uint8_t>(algo);
    option[2] = static_cast<uint8_t>(rdm);

    writeU64(option + 3, replay);
    writeU32(option + 11, key.first);

    // Zero out auth digest for digest calculation
    std::memset(option + 15, 0, headerSize - 15);

    tlv.append(Variable::Dhcp::Option::authentication, headerSize, nullptr, headerSize);

    // Return info for calculating digest later
    return DelayedAuthInfo{
        .digest = option + 15,
        .key = key.second,
        .algo = algo
    };
}

void Protocol::Dhcpv6::ClientAuthManager::addDelayedAuthDigest(Dhcpv6Header& dhcp, TLV16BufferManager& tlv, DelayedAuthInfo& auth)
{
    if (auth.algo == AuthAlgorithm::HMACMD5)
        Authentication::generateMD5(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size());
    else
        Authentication::generateHMAC(auth.digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), reinterpret_cast<const uint8_t*>(auth.key.data()), auth.key.size(), Authentication::SHA::SHA1);
}

bool Protocol::Dhcpv6::ClientAuthManager::validateDelayedAuth(Dhcpv6Header& dhcp, TLV16Option* opt)
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

    uint64_t replay = readU64(buf + 3);
    uint64_t keyID = readU64(buf + 11);
    uint8_t mac[20];
    size_t macLen = opt->valueSize - 15;
    std::memcpy(mac, opt->value + 15, macLen);
    std::memset(const_cast<uint8_t*>(opt->value) + 15, 0, macLen); // Zero digest for validation

    if (macLen != (algo == AuthAlgorithm::HMACMD5 ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH) || !isReplayValid(replay))
        return false;
    updateReplayCounter(replay);

    auto keyOpt = getDelayedKey(keyID);
    if (!keyOpt) return false;

    const auto& key = *keyOpt;
    size_t dhcpSize = static_cast<size_t>(opt->value + opt->valueSize - dhcp.buffer);
    if (algo == AuthAlgorithm::HMACMD5)
        Authentication::generateMD5(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.second.data()), key.second.size());
    else
        Authentication::generateHMAC(const_cast<uint8_t*>(opt->value) + 15, dhcp.buffer, dhcpSize, reinterpret_cast<const uint8_t*>(key.second.data()), key.second.size(), Authentication::SHA::SHA1);

    return std::memcmp(mac, opt->value + 15, macLen) == 0;
}

const uint8_t* Protocol::Dhcpv6::ClientAuthManager::addRkapAuthOption(TLV16BufferManager& tlv, const uint8_t* key)
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

    tlv.append(Variable::Dhcpv6::Options::auth, 20, nullptr, 20);

    return option + 4;
}

void Protocol::Dhcpv6::ClientAuthManager::addRkapAuthDigest(Dhcpv6Header& dhcp, TLV16BufferManager& tlv, const uint8_t* key, uint8_t* digest)
{
    Authentication::generateMD5(digest, dhcp.buffer, dhcp.fixedSize + tlv.size(), key, 16);
}

uint8_t* Protocol::Dhcpv6::ClientAuthManager::validateRkapDigest(Dhcpv6Header& dhcp, TLV16Option* opt)
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
