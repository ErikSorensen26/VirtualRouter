/**
 * @file KeyChainManager.h
 * @brief Registry and lifecycle manager for all key chains in the system.
 */

#ifndef KEY_CHAIN_MANAGER_H
#define KEY_CHAIN_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>

namespace security::authentication
{
class KeyChain;
enum class HmacType : int;

/**
 * @brief Owns and indexes all @ref KeyChain instances configured on the router.
 * @ingroup SECURITY_KEYS
 *
 * Provides creation, lookup, and removal of key chains. Routing protocols
 * (EIGRP, OSPF) reference key chains by pointer; the manager is the single
 * authoritative owner of all chain objects.
 *
 * Each instance contains:
 * - A flat list of heap-allocated @ref KeyChain objects.
 *
 * ## Architectural Role
 * The manager sits between the CLI/configuration layer and the routing
 * protocol instances. The CLI calls @ref create and @ref remove; protocol
 * instances call @ref lookup to obtain a pointer before starting
 * authentication. The manager does not participate in packet processing.
 *
 * ## Lifecycle & Ownership
 * `KeyChainManager` heap-allocates each @ref KeyChain via @ref create and
 * deletes them in the destructor and in @ref remove. Callers that hold a
 * pointer returned by @ref lookup must not dereference it after calling
 * @ref remove for that chain.
 *
 * @warning This class is not thread-safe. All mutations (create/remove) must
 *          be serialized with respect to concurrent lookups, typically under
 *          the configuration lock held by the CLI thread.
 *
 * @see KeyChain
 */
class KeyChainManager
{
public:
    KeyChainManager() = default;

    /**
     * @brief Destroys the manager and deletes all owned key chains.
     *
     * Any pointers previously returned by @ref lookup become dangling after
     * this destructor runs. Protocol instances must be torn down before the
     * manager is destroyed.
     */
    ~KeyChainManager();

    /**
     * @brief Creates a new key chain with the given name and adds it to the registry.
     *
     * The returned pointer is owned by this manager; do not delete it. If a
     * chain with the same name already exists, behavior is undefined — callers
     * should check via @ref lookup before calling @ref create.
     *
     * @param name  Human-readable name for the new chain; used for CLI lookups.
     * @return Pointer to the newly created @ref KeyChain.
     */
    KeyChain* create(const std::string& name);

    /**
     * @brief Validates an HMAC tag by delegating to the key chain identified by @p keyId.
     *
     * Looks up the chain whose @ref KeyChain::chainID equals @p keyId, then
     * calls @ref KeyChain::validate. Returns false if no matching chain exists.
     *
     * @param hmac      Received HMAC tag to verify.
     * @param computed  Scratch buffer for local digest recomputation.
     * @param keyId     Chain ID to look up.
     * @param data      Authenticated data.
     * @param size      Length of @p data in bytes.
     * @param type      Hash algorithm and tag length selector.
     * @return True if the tag is valid; false if the chain is not found or the tag mismatches.
     */
    bool validate(const uint8_t* hmac, uint8_t* computed, uint32_t keyId, const uint8_t* data, size_t size, const HmacType& type) const;

    /**
     * @brief Looks up a key chain by its numeric chain ID.
     *
     * @param id  The @ref KeyChain::chainID to search for.
     * @return Pointer to the matching chain, or nullptr if not found.
     */
    KeyChain* lookup(uint32_t id) const noexcept;

    /**
     * @brief Looks up a key chain by name.
     *
     * @param name  The @ref KeyChain::name to search for.
     * @return Pointer to the matching chain, or nullptr if not found.
     */
    KeyChain* lookup(const std::string& name) const noexcept;

    /**
     * @brief Removes and deletes the key chain identified by @p id.
     *
     * All pointers previously returned by @ref lookup for this chain become
     * dangling after this call.
     *
     * @param id  The @ref KeyChain::chainID of the chain to remove.
     */
    void remove(uint32_t id);

    /**
     * @brief Removes and deletes the key chain identified by @p name.
     *
     * All pointers previously returned by @ref lookup for this chain become
     * dangling after this call.
     *
     * @param name  The @ref KeyChain::name of the chain to remove.
     */
    void remove(const std::string& name);

private:
    std::vector<KeyChain*> chains; ///< Heap-allocated key chain objects; manager owns all entries.
};

} // namespace security::authentication

#endif // KEY_CHAIN_MANAGER_H
