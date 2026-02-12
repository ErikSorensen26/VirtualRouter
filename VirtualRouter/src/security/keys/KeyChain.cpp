// KeyChain.cpp

#include "security/Encryption.hpp"
#include "KeyChain.h"

namespace Authentication
{
bool KeyChain::validate(const uint8_t* hmac, uint8_t* computed, uint32_t keyId, const uint8_t* data, size_t size, const Authentication::HmacType type) const
{
    auto key = getCurrentSendKey();
    if (!key.has_value() || keyId != key.value().keyId)
        return false;

    Authentication::generateHMAC(
        computed,
        data,
        size,
        reinterpret_cast<const uint8_t*>(key.value().keyString.data()),
        key.value().keyString.size(),
        type
    );

    return std::memcmp(hmac, computed, static_cast<size_t>(type)) == 0;
}
};
