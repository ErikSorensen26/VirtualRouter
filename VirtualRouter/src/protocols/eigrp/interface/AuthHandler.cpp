// EigrpInterfaceAuth.cpp

#include "AuthHandler.h"
#include <shared_mutex>
#include "EigrpInterface.h"
#include <EigrpTypes.hpp>
#include <Encryption.hpp>
#include <PacketStructure.h>

namespace Eigrp
{
AuthHandler::AuthHandler(EigrpConfigs::InterfaceConfigs& configs) : configs(configs) {}

void AuthHandler::setKeyChain(uint8_t* keyId, const std::string* key, EigrpConfigs::AuthType* type, bool enable)
{
    std::unique_lock<std::shared_mutex> lock(configs.configsMutex);

    if (!enable)
    {
        configs.authKey.authType = EigrpConfigs::AuthType::NONE;
        configs.authKey.key = "";
        configs.authKey.keyId = 0;
    }

    if (keyId) configs.authKey.keyId = *keyId;
    if (key) configs.authKey.key = *key;
    if (type) configs.authKey.authType = *type;

    if (configs.authKey.authType != EigrpConfigs::AuthType::NONE &&
        configs.authKey.key != "" && 
        configs.authKey.keyId != 0)
    {
        configs.authKey.fullyEnabled.store(true, std::memory_order_release);
    }
}

uint8_t AuthHandler::buildAuthTLV(uint8_t* out)
{
    uint8_t keyId;
    EigrpConfigs::AuthType authType;
    std::string key;

    {
        if (!configs.authKey.fullyEnabled.load(std::memory_order_relaxed))
            return 0;

        std::shared_lock lock(configs.configsMutex);
        keyId = configs.authKey.keyId;
        authType = configs.authKey.authType;
        key = configs.authKey.key;
    }

    uint16_t hmacLength = 0;
    switch (authType)
    {
        case EigrpConfigs::AuthType::MD5: hmacLength = MD5_DIGEST_LENGTH; break;
        case EigrpConfigs::AuthType::SHA1: hmacLength = SHA_DIGEST_LENGTH; break;
        case EigrpConfigs::AuthType::SHA256: hmacLength = SHA256_DIGEST_LENGTH; break;
        case EigrpConfigs::AuthType::SHA384: hmacLength = SHA384_DIGEST_LENGTH; break;
        case EigrpConfigs::AuthType::SHA512: hmacLength = SHA512_DIGEST_LENGTH; break;
        case EigrpConfigs::AuthType::NONE: break;
    }

    out[0] = static_cast<uint8_t>(authType);
    out[1] = keyId;
    writeU16(out + 2, hmacLength);
    writeU32(out + 6, configs.authKey.replay.load(std::memory_order_relaxed));
    configs.authKey.replay.fetch_add(1, std::memory_order_seq_cst);
    std::memset(out + 8, 0, 8);

    out[16 + hmacLength] = static_cast<uint8_t>(key.size());
    std::memcpy(out + 17 + hmacLength, key.data(), key.size());

    return static_cast<uint8_t>(16 + hmacLength);
}

bool AuthHandler::validateAuth(const uint8_t* packetStart, TLV16Option& authOpt)
{
    if (authOpt.length < 16)
        return false;

    uint8_t authType = authOpt.value[0];
    uint8_t keyId = authOpt.value[1];
    uint32_t digestLen = readU16(authOpt.value + 2);
    uint32_t recvReplay = readU32(authOpt.value + 4);
    uint8_t* digest = const_cast<uint8_t*>(authOpt.value + 12);

    uint16_t hmacLength = 0;
    switch((EigrpConfigs::AuthType)authType)
    {
        case EigrpConfigs::AuthType::MD5: hmacLength = MD5_DIGEST_LENGTH;
        case EigrpConfigs::AuthType::SHA1: hmacLength = SHA_DIGEST_LENGTH;
        case EigrpConfigs::AuthType::SHA256: hmacLength = SHA256_DIGEST_LENGTH;
        case EigrpConfigs::AuthType::SHA384: hmacLength = SHA384_DIGEST_LENGTH;
        case EigrpConfigs::AuthType::SHA512: hmacLength = SHA512_DIGEST_LENGTH;
        default: return false;
    }

    if (digestLen != hmacLength)
        return false;

    std::string key;
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        if (keyId != configs.authKey.keyId)
            return false;
        key = configs.authKey.key;
        if (configs.authKey.authType != (EigrpConfigs::AuthType)authType)
            return false;
    }

    // Replay Protection
    if (recvReplay <= configs.authKey.lastReplay.load(std::memory_order_relaxed))
        return false;
    configs.authKey.lastReplay.store(recvReplay, std::memory_order_release);

    // Save and clear digest field
    uint8_t saved[SHA512_DIGEST_LENGTH];
    std::memcpy(saved, digest, hmacLength);
    std::memset(digest, 0, hmacLength);

    size_t totalLen = static_cast<size_t>((digest + hmacLength) - packetStart);

    switch ((EigrpConfigs::AuthType)authType)
    {
        case EigrpConfigs::AuthType::MD5:
            Authentication::generateMD5(digest, packetStart, totalLen,
                                        reinterpret_cast<const uint8_t*>(key.data()), key.size());
            break;
        case EigrpConfigs::AuthType::SHA1:
            Authentication::generateHMAC(digest, packetStart, totalLen,
                                         reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                                         Authentication::SHA::SHA1);
            break;
        case EigrpConfigs::AuthType::SHA256:
            Authentication::generateHMAC(digest, packetStart, totalLen,
                                         reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                                         Authentication::SHA::SHA256);
            break;
        case EigrpConfigs::AuthType::SHA384:
            Authentication::generateHMAC(digest, packetStart, totalLen,
                                         reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                                         Authentication::SHA::SHA384);
            break;
        case EigrpConfigs::AuthType::SHA512:
            Authentication::generateHMAC(digest, packetStart, totalLen,
                                         reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                                         Authentication::SHA::SHA512);
            break;
        default:
            return false;
    }

    // Constant-time compare
    uint8_t diff = 0;
    for (size_t i = 0; i < hmacLength; ++i)
        diff |= (digest[i] ^ saved[i]);

    return diff == 0;
}

void AuthHandler::appendAuthHMAC(uint8_t* packetStart)
{
    const uint8_t* ipHeader = packetStart;
    uint8_t ipHeaderLen = (ipHeader[0] & 0x0F) * 4;
    uint8_t* eigrpStart = packetStart + ipHeaderLen;

    uint8_t* cursor = eigrpStart + 20;
    uint8_t* authTLV = nullptr;
    uint16_t hmacLength = 0;

    // Locate the last TLV
    while (true)
    {
        uint16_t type = readU16(cursor);
        uint16_t length = readU16(cursor + 2);

        if (type == Variable::Eigrp::Option::authentication)
        {
            authTLV = cursor;
            hmacLength = readU16(eigrpStart + 6);
            break;
        }

        if (length == 0 || length > 2048)
            break;

        cursor += length;
    }

    if (!authTLV) return;

    // Zero out HMAC field temporarily
    uint8_t* hmacField = authTLV + 16; // starts after fixed TLV body
    std::memset(hmacField, 0x00, hmacLength);

    // Find the hmac key
    uint8_t keySize = hmacField[hmacLength];
    uint8_t* key = hmacField + hmacLength + 1;

    // Step 4: Compute HMAC over entire IP + EIGRP packet
    size_t totalLen = static_cast<size_t>((hmacField + hmacLength) - packetStart);

    switch (hmacLength)
    {
        case MD5_DIGEST_LENGTH:
            Authentication::generateMD5(hmacField, packetStart, totalLen, key, keySize);
            break;
        case SHA_DIGEST_LENGTH:
            Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA1);
            break;
        case SHA256_DIGEST_LENGTH:
            Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA256);
            break;
        case SHA384_DIGEST_LENGTH:
            Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA384);
            break;
        case SHA512_DIGEST_LENGTH:
            Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA512);
            break;
    }
}
}
