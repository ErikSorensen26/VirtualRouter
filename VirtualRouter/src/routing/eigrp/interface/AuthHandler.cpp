// EigrpInterfaceAuth.cpp

#include <Global.h>
#include "AuthHandler.h"
#include "EigrpInterface.h"
#include "security/Encryption.hpp"
#include "security/keys/KeyChainManager.h"
#include "security/keys/KeyChain.h"

namespace routing::eigrp
{
AuthHandler::AuthHandler(config::EigrpInterfaceRegistry& configs, security::authentication::KeyChainManager& keyMgr) : configs(configs), keyMgr(keyMgr) {}

uint16_t AuthHandler::buildAuthTLV(uint8_t* out)
{
    config::eigrp::AuthType authType = configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().load();
    if (authType == config::eigrp::AuthType::NONE)
        return 0;

    uint16_t digestLen = 0;
    switch (authType)
    {
        case config::eigrp::AuthType::MD5: digestLen = security::authentication::MD5_DIGEST_LENGTH; break;
        case config::eigrp::AuthType::SHA256: digestLen = security::authentication::SHA256_DIGEST_LENGTH; break;
        default: return 0;
    }

    utils::write<uint16_t>(out, static_cast<uint16_t>(authType));
    utils::write<uint16_t>(out + 2, digestLen);
    std::memset(out + 4, 0, 16 + digestLen);

    if (authType == config::eigrp::AuthType::SHA256 && configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().hasValue())
    {
        std::string keyPayload = configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().load();
        if (keyPayload.empty() || keyPayload.size() > 32)
            return 0;
        std::memcpy(out + 20, keyPayload.data(), keyPayload.size());
    }

    return 20 + digestLen;
}

bool AuthHandler::validateAuth(const uint8_t* packetStart, size_t size, const packet::TLV16Option* authOpt)
{
    if (configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().load() == config::eigrp::AuthType::NONE)
        return true;
    if (!authOpt)
        return false;

    config::eigrp::AuthType authType = static_cast<config::eigrp::AuthType>(utils::read<uint16_t>(authOpt->value));
    uint16_t digestLen = utils::read<uint16_t>(authOpt->value + 2);

    if (authType != configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().load())
        return false;
    if (authType == config::eigrp::AuthType::MD5 && (authOpt->valueSize != 36 || digestLen != 16))
        return false;
    if (authType == config::eigrp::AuthType::SHA256 && (authOpt->valueSize != 52 || digestLen != 32))
        return false;

    uint8_t* digestIdx = const_cast<uint8_t*>(authOpt->value) + 20;
    uint8_t digest[security::authentication::SHA256_DIGEST_LENGTH] = {0};
    std::memcpy(digest, digestIdx, digestLen);
    std::memset(digestIdx, 0, digestLen);

    bool hasKeychain = configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().hasValue();

    if (authType == config::eigrp::AuthType::MD5 && hasKeychain)
    {
        std::string chainName = configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().load();
        const auto* key = keyMgr.lookup(chainName);
        if (!key) return false;
        uint8_t computed[security::authentication::MD5_DIGEST_LENGTH];
        uint32_t keyId = utils::read<uint32_t>(authOpt->value + 4);
        return key->validate(digest, computed, keyId, packetStart, size, security::authentication::HmacType::MD5);
    }
    else if (authType == config::eigrp::AuthType::SHA256 && hasKeychain)
    {
        uint8_t computed[security::authentication::SHA256_DIGEST_LENGTH];
        std::string key = configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().load();
        security::authentication::generateHMAC(
            computed,
            packetStart,
            size,
            reinterpret_cast<const uint8_t*>(key.data()),
            key.size(),
            security::authentication::HmacType::SHA256
        );
        return std::memcmp(digest, computed, security::authentication::SHA256_DIGEST_LENGTH) == 0;
    }
    return false;
}

bool AuthHandler::appendAuthHMAC(core::Global& global, const std::string& chainName, uint8_t* packetStart, size_t size)
{
    const uint8_t* ipHeader = packetStart;
    uint8_t ipHeaderLen = (ipHeader[0] & 0x0F) * 4;
    uint8_t* eigrpStart = packetStart + ipHeaderLen;

    uint8_t* cursor = eigrpStart + 20;
    uint8_t* authTLV = nullptr;

    // Locate the last TLV
    while (true)
    {
        uint16_t type = utils::read<uint16_t>(cursor);
        uint16_t length = utils::read<uint16_t>(cursor + 2);

        if (type == EIGRP_OPTION_AUTHENTICATION)
        {
            authTLV = cursor + 4;
            config::eigrp::AuthType authType = static_cast<config::eigrp::AuthType>(utils::read<uint16_t>(authTLV));
            uint16_t digestLen = utils::read<uint16_t>(authTLV + 2);

            if (authType == config::eigrp::AuthType::MD5 && (length != 36 || digestLen != 16))
                return false;
            if (authType == config::eigrp::AuthType::SHA256 && (length != 52 || digestLen != 32))
                return false;

            if (authType == config::eigrp::AuthType::SHA256)
            {
                if (authTLV[20] == 0x00) return false;
                uint8_t pass[32] = {0};
                std::memcpy(pass, authTLV + 20, 32);
                size_t len = 0;
                while (len < 32 && pass[len] != 0) ++len;

                security::authentication::generateHMAC(
                    authTLV + 20,
                    packetStart,
                    size,
                    pass,
                    len,
                    security::authentication::HmacType::SHA256
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
                utils::write<uint32_t>(authTLV + 4, key.value().keyId);

                security::authentication::generateHMAC(
                    authTLV + 20,
                    packetStart,
                    size,
                    reinterpret_cast<const uint8_t*>(key.value().keyString.data()),
                    key.value().keyString.size(),
                    security::authentication::HmacType::MD5
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
} // namespace routing
