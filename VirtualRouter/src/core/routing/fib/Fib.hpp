/**
 * @file Fib.hpp
 * @brief RCU-protected Forwarding Information Base backed by an LPC-Trie.
 */

/**
 * @defgroup CORE_ROUTING_FIB Core Routing FIB
 * @ingroup CORE_ROUTING
 * @brief Forwarding Information Base: RCU-protected LPC-Trie for fast packet forwarding.
 */

#ifndef FIB_HPP
#define FIB_HPP

#include <atomic>
#include <cstdint>
#include <LpcTrie.hpp>

#include "routing/rib/RibEntry.hpp"

namespace core
{

/**
 * @brief Forwarding Information Base (FIB) for a single address family.
 *
 * Fib is a lock-free prefix trie used by the data plane to perform
 * longest-prefix-match (LPM) forwarding lookups.  Each leaf stores a
 * `std::atomic<RibEntry<Addr>*>` that is updated by the RIB after every
 * best-path selection.  Readers acquire no lock — they simply load the
 * atomic pointer under an RCU read-side guard.  Writers (the RIB) swap the
 * pointer and schedule deferred deletion of the old value via `RCU::retire`.
 *
 * ## Architectural Role
 * The FIB is owned by `Rib<AddrType>`, which keeps it consistent with the
 * RIB table.  `RoutingTable` exposes `lookup()` to the data plane, wrapping
 * the call with an `RCU::Guard`.
 *
 * ## Lifecycle & Ownership
 * Created and destroyed with the owning `Rib`.  All entries are heap-allocated
 * `RibEntry` copies managed via RCU retire; callers must not free them
 * directly.
 *
 * ## Concurrency Model
 * - `lookup()` — lock-free, safe from any thread under an `RCU::Guard`.
 * - `insert()` / `erase()` / `clear()` — called only from the RIB's
 *   serialised `ProcessQueue`; no concurrent writer is assumed.
 *
 * @tparam Addr Unsigned integral address type (`uint32_t` for IPv4,
 *              `__uint128_t` for IPv6).
 *
 * @see Rib
 * @see RibBucket
 */
template<typename Addr>
class Fib
{
public:
    /// Atomic slot type stored in the trie leaf; the RIB swaps these.
    using FibEntry = std::atomic<RibEntry<Addr>*>;

    // LOOKUP

    /**
     * @brief Longest-prefix-match lookup using a raw network-order byte span.
     * @param addr Network-order byte representation of the destination address.
     * @return Pointer to the best-matching `RibEntry`, or `nullptr` if no
     *         route exists.  Valid only within the caller's `RCU::Guard`.
     */
    RibEntry<Addr>* lookup(const types::NetworkSpan<Addr>& addr) const
    {
        FibEntry* fe = tree.lookup(addr);
        return fe ? fe->load(std::memory_order_relaxed) : nullptr;
    }

    /**
     * @brief Longest-prefix-match lookup using an integer address value.
     * @param a Destination address in host byte order.
     * @return Pointer to the best-matching `RibEntry`, or `nullptr`.
     */
    RibEntry<Addr>* lookup(Addr a) const
    {
        return lookup(reinterpret_cast<const types::NetworkSpan<Addr>&>(a));
    }

    // MODIFICATION

    /**
     * @brief Insert a prefix into the trie (byte-array form).
     * @param pfx  Network-order prefix bytes.
     * @param len  Prefix length in bits.
     * @param ribEntry Pointer to the atomic entry slot owned by `RibBucket`.
     * @return `true` on success, `false` if the prefix already exists.
     */
    bool insert(const uint8_t* pfx, uint8_t len, FibEntry* ribEntry)
    {
        return tree.insert(pfx, len, ribEntry);
    }

    /**
     * @brief Insert a prefix into the trie (integer form).
     * @param pfx  Prefix address value.
     * @param len  Prefix length in bits.
     * @param ribEntry Pointer to the atomic entry slot owned by `RibBucket`.
     * @return `true` on success.
     */
    bool insert(Addr pfx, uint8_t len, FibEntry* ribEntry)
    {
        uint8_t bytes[sizeof(Addr)];
        toBytes(pfx, bytes);
        return insert(bytes, len, ribEntry);
    }

    /**
     * @brief Remove a prefix from the trie (byte-array form).
     * @param p Network-order prefix bytes.
     * @param l Prefix length in bits.
     * @return `true` if the entry was found and removed.
     */
    bool erase(const uint8_t* p, uint8_t l)
    {
        return tree.erase(p, l);
    }

    /**
     * @brief Remove a prefix from the trie (integer form).
     * @param pfx Prefix address value.
     * @param l   Prefix length in bits.
     * @return `true` if the entry was found and removed.
     */
    bool erase(Addr pfx, uint8_t l)
    {
        uint8_t bytes[sizeof(Addr)];
        toBytes(pfx, bytes);
        return erase(bytes, l);
    }

    /**
     * @brief Remove all entries from the trie.
     */
    void clear() { tree.clear(); }

    // UTILITIES

    /**
     * @brief Convert an integer address to its big-endian byte representation.
     * @param a   Address value.
     * @param out Output buffer of at least `sizeof(Addr)` bytes.
     */
    static void toBytes(Addr a, uint8_t* out) noexcept
    {
        for (uint8_t i = 0; i < sizeof(Addr); ++i)
            out[i] = static_cast<uint8_t>(a >> ((sizeof(Addr) - 1 - i) * 8));
    }

private:
    static constexpr uint8_t W = sizeof(Addr)*8; ///< Address width in bits.

    types::LPCTrie<sizeof(Addr), FibEntry, 8, true> tree; ///< Underlying LPC-Trie.
};

} // namespace core

#endif // FIB_HPP
