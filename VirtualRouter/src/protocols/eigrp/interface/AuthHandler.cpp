// EigrpInterfaceAuth.cpp

#include "AuthHandler.h"
#include "global/Global.h"
#include "EigrpInterface.h"
#include "security/Encryption.hpp"
#include "security/keys/KeyChainManager.h"
#include "security/keys/KeyChain.h"

namespace EIGRP
{
AuthHandler::AuthHandler(Config::EigrpInterfaceRegistry& configs, Authentication::KeyChainManager& keyMgr) : configs(configs), keyMgr(keyMgr) {}

uint16_t AuthHandler::buildAuthTLV(uint8_t* out)
{
    EIGRP::AuthType authType = configs.get<Config::EigrpInterface::AUTHENTICATION_MODE>().load();
    if (authType == EIGRP::AuthType::NONE)
        return 0;

    uint16_t digestLen = 0;
    switch (authType)
    {
        case EIGRP::AuthType::MD5: digestLen = MD5_DIGEST_LENGTH; break;
        case EIGRP::AuthType::SHA256: digestLen = SHA256_DIGEST_LENGTH; break;
        default: return 0;
    }

    writeU16(out, static_cast<uint16_t>(authType));
    writeU16(out + 2, digestLen);
    std::memset(out + 4, 0, 16 + digestLen);

    if (authType == EIGRP::AuthType::SHA256 && configs.get<Config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().hasValue())
    {
        std::string keyPayload = configs.get<Config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().load();
        if (keyPayload.empty() || keyPayload.size() > 32)
            return 0;
        std::memcpy(out + 20, keyPayload.data(), keyPayload.size());
    }

    return 20 + digestLen;
}

bool AuthHandler::validateAuth(const uint8_t* packetStart, size_t size, const TLV16Option* authOpt)
{
    if (configs.get<Config::EigrpInterface::AUTHENTICATION_MODE>().load() == EIGRP::AuthType::NONE)
        return true;
    if (!authOpt)
        return false;

    EIGRP::AuthType authType = static_cast<EIGRP::AuthType>(readU16(authOpt->value));
    uint16_t digestLen = readU16(authOpt->value + 2);

    if (authType != configs.get<Config::EigrpInterface::AUTHENTICATION_MODE>().load())
        return false;
    if (authType == EIGRP::AuthType::MD5 && (authOpt->valueSize != 36 || digestLen != 16))
        return false;
    if (authType == EIGRP::AuthType::SHA256 && (authOpt->valueSize != 52 || digestLen != 32))
        return false;

    uint8_t* digestIdx = const_cast<uint8_t*>(authOpt->value) + 20;
    uint8_t digest[SHA256_DIGEST_LENGTH] = {0};
    std::memcpy(digest, digestIdx, digestLen);
    std::memset(digestIdx, 0, digestLen);

    bool hasKeychain = configs.get<Config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().hasValue();

    if (authType == EIGRP::AuthType::MD5 && hasKeychain)
    {
        std::string chainName = configs.get<Config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().load();
        const auto* key = keyMgr.lookup(chainName);
        if (!key) return false;
        uint8_t computed[MD5_DIGEST_LENGTH];
        uint32_t keyId = readU32(authOpt->value + 4);
        return key->validate(digest, computed, keyId, packetStart, size, Authentication::HmacType::MD5);
    }
    else if (authType == EIGRP::AuthType::SHA256 && hasKeychain)
    {
        uint8_t computed[SHA256_DIGEST_LENGTH];
        std::string key = configs.get<Config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().load();
        Authentication::generateHMAC(
            computed,
            packetStart,
            size,
            reinterpret_cast<const uint8_t*>(key.data()),
            key.size(),
            Authentication::HmacType::SHA256
        );
        return std::memcmp(digest, computed, SHA256_DIGEST_LENGTH) == 0;
    }
    return false;
}

bool AuthHandler::appendAuthHMAC(Global& global, const std::string& chainName, uint8_t* packetStart, size_t size)
{
    const uint8_t* ipHeader = packetStart;
    uint8_t ipHeaderLen = (ipHeader[0] & 0x0F) * 4;
    uint8_t* eigrpStart = packetStart + ipHeaderLen;

    uint8_t* cursor = eigrpStart + 20;
    uint8_t* authTLV = nullptr;

    // Locate the last TLV
    while (true)
    {
        uint16_t type = readU16(cursor);
        uint16_t length = readU16(cursor + 2);

        if (type == EIGRP_OPTION_AUTHENTICATION)
        {
            authTLV = cursor + 4;
            EIGRP::AuthType authType = static_cast<EIGRP::AuthType>(readU16(authTLV));
            uint16_t digestLen = readU16(authTLV + 2);

            if (authType == EIGRP::AuthType::MD5 && (length != 36 || digestLen != 16))
                return false;
            if (authType == EIGRP::AuthType::SHA256 && (length != 52 || digestLen != 32))
                return false;

            if (authType == EIGRP::AuthType::SHA256)
            {
                if (authTLV[20] == 0x00) return false;
                uint8_t pass[32] = {0};
                std::memcpy(pass, authTLV + 20, 32);
                size_t len = 0;
                while (len < 32 && pass[len] != 0) ++len;

                Authentication::generateHMAC(
                    authTLV + 20,
                    packetStart,
                    size,
                    pass,
                    len,
                    Authentication::HmacType::SHA256
                );
                std::memset(pass, 0, 32);
            }
            else
            {
                std::memset(authTLV + 20, 0, 2);
                const auto* chain = global.keyChainManager.lookup(chainName);
                if (!chain) return false;
                auto key = chain->getCurrentSendKey();
                if (!key.has_value()) return false;
                writeU32(authTLV + 4, key.value().keyId);

                Authentication::generateHMAC(
                    authTLV + 20,
                    packetStart,
                    size,
                    reinterpret_cast<const uint8_t*>(key.value().keyString.data()),
                    key.value().keyString.size(),
                    Authentication::HmacType::MD5
                );
            }

            return true;
        }

        if (length == 0 || length > 2048)
            break;

        cursor += length;
    }
    return true;
}
}
