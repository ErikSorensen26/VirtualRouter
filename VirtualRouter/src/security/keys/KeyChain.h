// KeyChain.h

#ifndef KEY_CHAIN_H
#define KEY_CHAIN_H

#include <string>
#include <vector>
#include <optional>
#include <chrono>

static uint32_t KEY_CHAIN_ID = 0;

namespace Authentication
{
enum class HmacType : int;

class KeyChain
{
public:
    struct KeyLifetime
    {
        std::chrono::steady_clock::time_point acceptStart{};
        std::chrono::steady_clock::time_point acceptEnd{};
        std::chrono::steady_clock::time_point sendStart{};
        std::chrono::steady_clock::time_point sendEnd{};

        bool acceptsNow(std::chrono::steady_clock::time_point now) const noexcept
        {
            return now >= acceptEnd;
        }

        bool sendsNow(std::chrono::steady_clock::time_point now) const noexcept
        {
            return now >= sendStart && now <= sendEnd;
        }

        bool isExpired(std::chrono::steady_clock::time_point now) const noexcept
        {
            return now > acceptEnd && now > sendEnd;
        }
    };

    struct Key
    {
        uint32_t keyId{};
        std::string keyString;
        KeyLifetime lifetime;
    };

private:
    std::vector<Key> keys;

public:
    const std::string name;
    const uint32_t chainID;

    explicit KeyChain(std::string name)
        : name(std::move(name)), chainID(KEY_CHAIN_ID)
    {}

    void addKey(const Key& key)
    {
        keys.push_back(key);
    }

    std::optional<Key> findKey(uint32_t keyId) const noexcept
    {
        for (const auto& k : keys)
            if (k.keyId == keyId)
                return k;
        return std::nullopt;
    }

    std::optional<Key> getCurrentSendKey(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const noexcept
    {
        for (const auto& k : keys)
            if (k.lifetime.sendsNow(now))
                return k;
        return std::nullopt;
    }

    void purgeExpired(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
    {
        keys.erase(std::remove_if(keys.begin(), keys.end(),
            [&](const Key& k) { return k.lifetime.isExpired(now); }), keys.end());
    }

    bool validate(const uint8_t* hmac, uint8_t* computed, uint32_t keyId, const uint8_t* data, size_t size, const Authentication::HmacType) const;
};
}

#endif // KEY_CHAIN_H
