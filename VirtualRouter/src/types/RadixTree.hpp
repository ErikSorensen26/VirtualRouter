/**
 * @file RadixTree.hpp
 * @brief Lock-free binary Patricia trie for longest-prefix-match IP lookups.
 */

#ifndef RADIX_TREE_HPP
#define RADIX_TREE_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <RCU.hpp>

namespace types
{

/**
 * @brief Lock-free (single-writer, multi-reader) binary Patricia trie keyed on
 *        fixed-width byte-array prefixes in network (big-endian) bit order.
 * @ingroup TYPES
 *
 * Each internal node stores exactly one discriminator bit index (@c Node::bit)
 * that routes the lookup left or right. A `memcmp` against the node's stored
 * prefix is performed only to confirm that a candidate node actually covers the
 * query address — never as part of the descent decision. This keeps the hot
 * lookup path branch-free apart from the bit-test.
 *
 * The tree is path-compressed: nodes whose single-child chains would waste
 * space are merged by raising the discriminator bit past the run of identical
 * bits, so tree depth is bounded by the number of distinct prefixes rather than
 * the address width.
 *
 * ## Architectural Role
 * RadixTree is the low-level substrate used by the RIB and FIB for both IPv4
 * (N=4) and IPv6 (N=16) prefix storage. It does not own the values it stores —
 * callers are responsible for value lifetime unless T is itself a value type.
 *
 * ## Lifecycle & Ownership
 * Constructed empty. @c insert / @c erase update the tree in-place; the root
 * pointer is an atomic so readers see a consistent snapshot without a lock.
 * @c clear destroys all nodes and resets the root to null.
 *
 * ## Concurrency Model
 * Single writer, multiple concurrent readers. When @c useRCU is @c true,
 * retired nodes are passed to @ref utils::RCU::retire so that readers holding
 * an RCU read-side critical section never observe freed memory. When
 * @c useRCU is @c false, nodes are deleted immediately and callers must
 * guarantee no concurrent readers exist during mutation.
 *
 * @warning Only one thread may call @c insert, @c erase, or @c clear at a
 *          time. Concurrent writes are not safe even when @c useRCU is @c true.
 *
 * @tparam N       Address width in bytes. Must be > 0.
 *                 Use 4 for IPv4, 16 for IPv6.
 * @tparam T       Value type stored at each prefix node. Must be
 *                 default-constructible (used to clear a node's value on
 *                 logical deletion when the node has children).
 * @tparam useRCU  When @c true, deferred node reclamation is routed through
 *                 @ref utils::RCU::retire. When @c false, nodes are deleted
 *                 immediately on removal.
 *
 * @see LPCTrie
 */
template<uint8_t N, typename T, bool useRCU = false>
class RadixTree
{
    static_assert(N > 0, "address width must be at least 1 byte");

public:
    /// Total number of addressable bits (N * 8).
    static constexpr uint16_t W = static_cast<uint16_t>(N) * 8;

    /**
     * @brief A single node in the Patricia trie.
     * @ingroup TYPES
     *
     * Each node holds a stored prefix, the prefix length, the discriminator
     * bit index used to route lookups, atomic child pointers, and an embedded
     * value. Nodes are heap-allocated and never moved; child pointers are
     * updated atomically so readers can traverse the tree without a lock.
     *
     * A bit index of @c W (equal to the address width in bits) marks a leaf:
     * no further descent is performed when @c bit >= W.
     */
    struct Node
    {
        uint8_t prefix[N];
        uint16_t bit;               ///< Discriminator bit index; W means leaf (no further descent).
        uint8_t length;             ///< Prefix length in bits [0, W].
        std::atomic<Node*> left{nullptr};
        std::atomic<Node*> right{nullptr};
        T                  value;

        /**
         * @brief Constructs a node with the given prefix, length, discriminator bit, and value.
         *
         * @param pfx Pointer to N bytes representing the network prefix (host-order within
         *            each byte, network order across bytes).
         * @param len Prefix length in bits.
         * @param b   Discriminator bit index for routing future lookups.
         * @param val Value to move into this node.
         */
        Node(const uint8_t* pfx, uint8_t len, uint16_t b, T&& val)
            : bit(b), length(len), value(std::forward<T>(val))
        {
            std::memcpy(prefix, pfx, N);
        }

        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;
    };

    /**
     * @brief Visits every node in the tree, invoking @p fn for each prefix entry.
     *
     * Traversal order is unspecified. The callback receives the raw prefix bytes,
     * the prefix length in bits, and a const reference to the stored value.
     *
     * @tparam F Callable with signature compatible with
     *           @c void(const uint8_t*, uint8_t, const T&).
     * @param fn Visitor function called once per node.
     */
    template <typename F>
    void forEach(F&& fn) const noexcept
    {
        forEachNode(root.load(std::memory_order_acquire), std::forward<F>(fn));
    }

    /**
     * @brief Finds the longest prefix in the tree that covers @p addr.
     *
     * Walks the trie using single-bit routing and records every node whose
     * stored prefix covers @p addr, returning a pointer to the value in the
     * deepest (longest) matching node.
     *
     * @param addr Pointer to N bytes of the address to look up, in network byte order.
     * @return Pointer to the matching value, or @c nullptr if no prefix covers @p addr.
     *
     * @note Safe to call from multiple threads concurrently with no mutation in progress,
     *       or any time when @c useRCU is @c true and the caller holds an RCU read guard.
     */
    const T* lookup(const uint8_t* addr) const noexcept
    {
        Node*    n    = root.load(std::memory_order_acquire);
        const T* best = nullptr;

        while (n)
        {
            if (prefixCovers(addr, n->prefix, n->length))
                best = &n->value;

            if (n->bit >= W) break;

            n = (bitAt(addr, n->bit) ? n->right : n->left)
                    .load(std::memory_order_acquire);
        }
        return best;
    }

    /**
     * @brief Finds a node whose prefix and length exactly match @p pfx / @p len.
     *
     * Unlike @c lookup, this does not return longer covering prefixes — it returns
     * a value only when the exact prefix/length pair is present in the tree.
     *
     * @param pfx Pointer to N bytes of the prefix to search for.
     * @param len Prefix length in bits.
     * @return Pointer to the stored value, or @c nullptr if not found.
     */
    const T* lookupExact(const uint8_t* pfx, uint8_t len) const noexcept
    {
        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* n = root.load(std::memory_order_acquire);
        while (n)
        {
            if (n->length == len && std::memcmp(n->prefix, masked, N) == 0)
                return &n->value;

            if (n->bit >= W) break;

            n = (bitAt(masked, n->bit) ? n->right : n->left)
                    .load(std::memory_order_acquire);
        }
        return nullptr;
    }

    /**
     * @brief Inserts or replaces a prefix/value pair in the tree.
     *
     * The host bits of @p pfx beyond @p len are masked to zero before
     * insertion to ensure canonical storage. If an identical prefix already
     * exists its value is overwritten in place; otherwise a new node is
     * allocated and linked into the trie.
     *
     * @param pfx   Pointer to N bytes of the network prefix.
     * @param len   Prefix length in bits [0, W].
     * @param value Value to store; moved into the node.
     * @return Always @c true (reserved for future error paths).
     *
     * @warning Must not be called concurrently with any other mutating operation.
     */
    bool insert(const uint8_t* pfx, uint8_t len, T&& value)
    {
        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* oldRoot = root.load(std::memory_order_acquire);
        Node* newRoot = insertRec(oldRoot, masked, len, std::forward<T>(value));

        if (newRoot != oldRoot)
            root.store(newRoot, std::memory_order_release);
        return true;
    }

    /**
     * @brief Removes the node matching exactly @p pfx / @p len.
     *
     * If the node has children it is logically erased by resetting its value
     * to a default-constructed @c T and remains in the tree as an internal
     * routing node. If it is a leaf the node is unlinked and retired.
     *
     * @param pfx Pointer to N bytes of the network prefix to remove.
     * @param len Prefix length in bits.
     * @return @c true if the prefix was found and removed, @c false if not present.
     *
     * @warning Must not be called concurrently with any other mutating operation.
     */
    bool erase(const uint8_t* pfx, uint8_t len)
    {
        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* r = root.load(std::memory_order_acquire);
        if (!r) return false;

        if (r->length == len && std::memcmp(r->prefix, masked, N) == 0)
        {
            Node* l  = r->left.load(std::memory_order_relaxed);
            Node* ri = r->right.load(std::memory_order_relaxed);
            if (!l && !ri)
            {
                Node* old = root.exchange(nullptr, std::memory_order_acq_rel);
                retire(old);
                return true;
            }
            r->value = T{};
            return true;
        }

        return eraseRec(r, masked, len) != EraseResult::NOT_FOUND;
    }

    /**
     * @brief Removes all nodes from the tree and resets it to an empty state.
     *
     * All nodes are retired via the configured reclamation strategy (immediate
     * delete or RCU-deferred). After this call @c lookup and @c forEach return
     * as though the tree was just constructed.
     *
     * @warning Must not be called concurrently with any other operation.
     */
    void clear()
    {
        Node* old = root.exchange(nullptr, std::memory_order_acq_rel);
        destroyAll(old);
    }

    /**
     * @brief Returns the value of bit @p i in address @p addr (network bit order).
     *
     * Bit 0 is the most-significant bit of @p addr[0]; bit 7 is the
     * least-significant bit of @p addr[0]; bit 8 is the MSB of @p addr[1], etc.
     *
     * @param addr Pointer to N bytes of the address.
     * @param i    Bit index [0, W). Returns @c false for i >= W.
     * @return @c true if the bit is set, @c false otherwise.
     */
    static bool bitAt(const uint8_t* addr, uint16_t i) noexcept
    {
        if (i >= W) return false;
        return (addr[i >> 3] >> (7 - (i & 7))) & 1;
    }

    /**
     * @brief Masks the host bits of @p pfx beyond @p len, writing the result to @p out.
     *
     * Bits [len, W) in @p out are set to zero. This canonicalises prefixes before
     * storage or comparison so that, e.g., 10.1.2.3/24 and 10.1.2.0/24 resolve to
     * the same key.
     *
     * @param pfx Pointer to N input bytes.
     * @param len Number of prefix bits to preserve [0, W].
     * @param out Pointer to N output bytes. May not alias @p pfx.
     */
    static void applyMask(const uint8_t* pfx, uint8_t len, uint8_t* out) noexcept
    {
        if (len >= W) { std::memcpy(out, pfx, N); return; }

        uint8_t full = len >> 3;
        uint8_t rem  = len & 7;

        if (full) std::memcpy(out, pfx, full);

        if (rem)
        {
            out[full] = pfx[full] & static_cast<uint8_t>(0xFF00u >> rem);
            ++full;
        }

        if (full < N) std::memset(out + full, 0, N - full);
    }

private:
    template <typename F>
    static void forEachNode(Node* n, F&& fn) noexcept
    {
        if (!n) return;
        fn(n->prefix, n->length, n->value);
        forEachNode(n->left.load(std::memory_order_acquire), fn);
        forEachNode(n->right.load(std::memory_order_acquire), fn);
    }

    /// Returns true if @p prefix/len covers (is a covering prefix of) @p addr.
    static bool prefixCovers(const uint8_t* addr,
                              const uint8_t* prefix,
                              uint8_t        len) noexcept
    {
        if (len == 0) return true;

        uint8_t full = len >> 3;
        uint8_t rem  = len & 7;

        if (full && std::memcmp(addr, prefix, full) != 0)
            return false;

        if (rem)
        {
            uint8_t m = static_cast<uint8_t>(0xFF00u >> rem);
            if ((addr[full] & m) != prefix[full])
                return false;
        }
        return true;
    }

    /**
     * @brief Returns the index of the first bit that differs between @p a and @p b.
     *
     * Returns @c W if the two N-byte arrays are identical, which encodes
     * "no divergence" and is used to mark leaf nodes.
     */
    static uint16_t diffbit(const uint8_t* a, const uint8_t* b) noexcept
    {
        for (uint8_t i = 0; i < N; ++i)
        {
            uint8_t x = a[i] ^ b[i];
            if (!x) continue;

            uint8_t pos = static_cast<uint8_t>(__builtin_clz((unsigned)x) - 24);
            return static_cast<uint16_t>(i * 8 + pos);
        }
        return W;
    }

    /**
     * @brief Recursive insertion helper; returns the (possibly new) subtree root.
     *
     * If @p n is null a fresh leaf is created. If the prefix matches @p n exactly
     * its value is updated. Otherwise the first differing bit determines whether
     * a new parent is spliced in above @p n, or the insertion descends to a child.
     */
    static Node* insertRec(Node* n, const uint8_t* pfx, uint8_t len, T&& value)
    {
        if (!n)
            return new Node(pfx, len, W, std::forward<T>(value));

        if (n->length == len && std::memcmp(n->prefix, pfx, N) == 0)
        {
            n->value = std::forward<T>(value);
            return n;
        }

        uint16_t d = diffbit(n->prefix, pfx);

        if (d < n->bit)
        {
            Node* parent = new Node(pfx, len, d, std::forward<T>(value));
            bool  dir    = bitAt(pfx, d);
            (dir ? parent->left : parent->right)
                .store(n, std::memory_order_relaxed);
            return parent;
        }

        bool dir = bitAt(pfx, n->bit);
        std::atomic<Node*>& childRef = dir ? n->right : n->left;
        Node* child    = childRef.load(std::memory_order_acquire);
        Node* newChild = insertRec(child, pfx, len, std::forward<T>(value));

        if (newChild != child)
            childRef.store(newChild, std::memory_order_release);

        return n;
    }

    /// Result codes returned by @c eraseRec to guide parent-level cleanup.
    enum class EraseResult : uint8_t
    {
        KEEP,       ///< Node still exists (has children or was logically erased).
        REMOVE,     ///< Node is a childless leaf and should be unlinked by the caller.
        NOT_FOUND,  ///< Prefix was not present in this subtree.
    };

    /**
     * @brief Recursive erasure helper.
     *
     * Descends toward the target prefix. If found with no children the node
     * signals @c REMOVE so the parent can unlink and retire it. If found with
     * children its value is cleared and @c KEEP is returned. Returns @c NOT_FOUND
     * if the prefix is absent.
     */
    EraseResult eraseRec(Node* n, const uint8_t* pfx, uint8_t len)
    {
        if (!n) return EraseResult::NOT_FOUND;

        if (n->length == len && std::memcmp(n->prefix, pfx, N) == 0)
        {
            bool hasChildren =
                n->left.load(std::memory_order_relaxed) ||
                n->right.load(std::memory_order_relaxed);

            if (!hasChildren) return EraseResult::REMOVE;

            n->value = T{};
            return EraseResult::KEEP;
        }

        if (n->bit >= W) return EraseResult::NOT_FOUND;

        bool dir = bitAt(pfx, n->bit);
        std::atomic<Node*>& childRef = dir ? n->right : n->left;
        Node* child = childRef.load(std::memory_order_relaxed);

        EraseResult st = eraseRec(child, pfx, len);

        if (st == EraseResult::REMOVE)
        {
            Node* removed = childRef.exchange(nullptr, std::memory_order_release);
            if (removed) retire(removed);
            return EraseResult::KEEP;
        }

        return st;
    }

    /// Post-order traversal that retires every node in the subtree rooted at @p n.
    void destroyAll(Node* n)
    {
        if (!n) return;
        destroyAll(n->left.load(std::memory_order_relaxed));
        destroyAll(n->right.load(std::memory_order_relaxed));
        retire(n);
    }

    static void destroy(Node* n) { delete n; }

    /**
     * @brief Reclaims @p n according to the @c useRCU policy.
     *
     * With @c useRCU=true the deletion lambda is handed to @ref utils::RCU::retire
     * so that readers currently inside a read-side critical section complete before
     * the memory is freed. With @c useRCU=false the node is deleted immediately.
     */
    static void retire(Node* n)
    {
        if (!n) return;
        if constexpr (useRCU)
            utils::RCU::retire([n]{ destroy(n); });
        else
            destroy(n);
    }

    std::atomic<Node*> root{nullptr}; ///< Root of the Patricia trie; null when empty.
};

} // namespace types

#endif // RADIX_TREE_HPP
