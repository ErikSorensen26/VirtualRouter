/**
 * @file KeyChain.h
 * @brief Named key chain holding time-bounded authentication keys.
 */

/**
 * @defgroup SECURITY_KEYS Security Keys
 * @ingroup SECURITY
 * @brief Key chain and key chain manager for authentication key storage.
 */

#ifndef KEY_CHAIN_H
#define KEY_CHAIN_H

#include <string>
#include <vector>
#include <optional>
#include <chrono>

#include "security/crypto/Hmac.hpp"

namespace security { static uint32_t KEY_CHAIN_ID = 0; }

/**
 * @namespace security::authentication
 * @brief Key-chain management and HMAC-based authentication for routing protocols.
 */
namespace security::authentication
{
using hmac::HmacType;

/**
 * @brief A named collection of time-bounded authentication keys for a routing protocol.
 * @ingroup SECURITY_KEYS
 *
 * A key chain groups one or more keys, each associated with independent
 * accept and send lifetime windows. Protocols (EIGRP, OSPF, BGP MD5) reference
 * a key chain by name and query it at packet-send or packet-receive time to
 * obtain the active key for the current moment.
 *
 * Each instance contains:
 * - A set of @ref Key entries, each with its own @ref KeyLifetime.
 * - A globally unique @ref chainID assigned at construction from the
 *   monotonically increasing @ref security::KEY_CHAIN_ID counter.
 *
 * ## Architectural Role
 * Key chains are owned by @ref KeyChainManager, which exposes lookup by both
 * name and numeric ID. Routing protocol instances hold a pointer to the chain
 * they are configured to use; they do not own it.
 *
 * ## Lifecycle & Ownership
 * Constructed and destroyed exclusively by @ref KeyChainManager. The @ref name
 * and @ref chainID are immutable after construction. Expired keys are removed
 * lazily via @ref purgeExpired, which callers may invoke periodically to bound
 * memory growth on long-running chains.
 *
 * @warning Key chain objects are not thread-safe. All accesses from multiple
 *          threads must be serialized externally by the owning manager.
 *
 * @see KeyChainManager
 */
class KeyChain
{
public:

    /**
     * @brief Accept and send lifetime windows for a single key.
     * @ingroup SECURITY_KEYS
     *
     * The accept window controls when this key is valid for verifying received
     * packets. The send window controls when this key should be used to sign
     * outgoing packets. The two windows are independent: a key may be accepted
     * for longer than it is sent (rolling transition period).
     */
    struct KeyLifetime
    {
        std::chrono::steady_clock::time_point acceptStart{}; ///< When this key begins to be accepted from peers.
        std::chrono::steady_clock::time_point acceptEnd{};   ///< When this key stops being accepted from peers.
        std::chrono::steady_clock::time_point sendStart{};   ///< When this key begins to be used for outgoing packets.
        std::chrono::steady_clock::time_point sendEnd{};     ///< When this key stops being used for outgoing packets.

        /**
         * @brief Returns true if @p now falls within the accept window.
         * @ingroup SECURITY_KEYS
         *
         * @param now  The current time to test against.
         */
        bool acceptsNow(std::chrono::steady_clock::time_point now) const noexcept
        {
            return now >= acceptEnd;
        }

        /**
         * @brief Returns true if @p now falls within the send window.
         *
         * @param now  The current time to test against.
         */
        bool sendsNow(std::chrono::steady_clock::time_point now) const noexcept
        {
            return now >= sendStart && now <= sendEnd;
        }

        /**
         * @brief Returns true if both the accept and send windows have passed.
         *
         * A key that is expired in both windows contributes nothing further and
         * may be removed by @ref KeyChain::purgeExpired.
         *
         * @param now  The current time to test against.
         */
        bool isExpired(std::chrono::steady_clock::time_point now) const noexcept
        {
            return now > acceptEnd && now > sendEnd;
        }
    };

    /**
     * @brief A single authentication key with its identifier, material, and lifetime.
     */
    struct Key
    {
        uint32_t    keyId{};      ///< Numeric key identifier sent in protocol packets.
        std::string keyString;    ///< Raw key material used as the HMAC secret.
        KeyLifetime lifetime;     ///< Accept and send lifetime windows for this key.
    };

private:
    std::vector<Key> keys; ///< Ordered list of keys; queried front-to-back for send/accept selection.

public:
    const std::string name;    ///< Human-readable name used for CLI configuration lookups.
    const uint32_t    chainID; ///< Globally unique ID assigned at construction.

    /**
     * @brief Constructs a key chain with the given name and assigns it a unique ID.
     *
     * The chain starts empty; keys are added via @ref addKey. The @ref chainID
     * is taken from the global @ref security::KEY_CHAIN_ID counter, which is
     * incremented after each construction.
     *
     * @param name  Human-readable key chain name; used by @ref KeyChainManager
     *              for name-based lookups.
     */
    explicit KeyChain(std::string name)
        : name(std::move(name)), chainID(security::KEY_CHAIN_ID)
    {}

    /**
     * @brief Appends a key to the chain.
     *
     * Keys are not sorted or deduplicated on insertion. If two keys share the
     * same @ref Key::keyId, the first one found during linear search is used.
     *
     * @param key  Key to add; copied into the internal list.
     */
    void addKey(const Key& key)
    {
        keys.push_back(key);
    }

    /**
     * @brief Looks up a key by its numeric key ID.
     *
     * @param keyId  The key identifier to search for.
     * @return The matching @ref Key, or `std::nullopt` if not found.
     */
    std::optional<Key> findKey(uint32_t keyId) const noexcept
    {
        for (const auto& k : keys)
            if (k.keyId == keyId)
                return k;
        return std::nullopt;
    }

    /**
     * @brief Returns the first key whose send window is active at @p now.
     *
     * Protocols call this immediately before signing an outgoing packet to
     * determine which key ID and material to stamp in the authentication field.
     *
     * @param now  The time point to evaluate; defaults to the current instant.
     * @return The active send key, or `std::nullopt` if no key is in its send window.
     */
    std::optional<Key> getCurrentSendKey(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const noexcept
    {
        for (const auto& k : keys)
            if (k.lifetime.sendsNow(now))
                return k;
        return std::nullopt;
    }

    /**
     * @brief Removes all keys whose accept and send windows have both expired.
     *
     * Intended to be called periodically (e.g. from a maintenance timer) to
     * prevent unbounded growth of long-lived key chains.
     *
     * @param now  The time point to evaluate expiry against; defaults to now.
     */
    void purgeExpired(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
    {
        keys.erase(std::remove_if(keys.begin(), keys.end(),
            [&](const Key& k) { return k.lifetime.isExpired(now); }), keys.end());
    }

    /**
     * @brief Verifies an HMAC tag against a locally recomputed digest for a given key.
     *
     * Looks up @p keyId in this chain, recomputes the HMAC over @p data using
     * the retrieved key material and @p type, writes the result into @p computed,
     * then performs a constant-time comparison against @p hmac.
     *
     * @param hmac      The received HMAC tag to verify (length determined by @p type).
     * @param computed  Scratch buffer for the locally computed digest; must be at
     *                  least `static_cast<int>(type)` bytes wide.
     * @param keyId     Key identifier to look up in this chain.
     * @param data      The data that was authenticated.
     * @param size      Length of @p data in bytes.
     * @param type      Hash algorithm; also determines the tag length.
     * @return True if the tag matches and the key was found; false otherwise.
     */
    bool validate(const uint8_t* hmac, uint8_t* computed, uint32_t keyId, const uint8_t* data, size_t size, const HmacType type) const;
};

} // namespace security::authentication

#endif // KEY_CHAIN_H
