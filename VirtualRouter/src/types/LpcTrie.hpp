/**
 * @file LpcTrie.hpp
 * @brief Level-Compressed Patricia Trie for high-performance IP longest-prefix-match.
 */

// LPCTrie.hpp
#pragma once
#include <atomic>
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <RCU.hpp>
#include <NetworkSpan.hpp>

namespace types
{

/**
 * @brief Level-Compressed Patricia Trie (LC-Trie) for IP prefix lookup with
 *        configurable stride width and optional RCU-based safe reclamation.
 * @ingroup TYPES
 *
 * The LC-Trie extends a standard Patricia trie by consuming @c S bits per
 * level (the "stride") rather than one bit at a time. Each node fans out to
 * at most 2^S children, dramatically reducing tree depth at the cost of
 * slightly wider nodes. Path compression (skip bits) collapses runs of
 * single-child levels so that empty subtrees never appear in the tree.
 *
 * Each node keeps a sorted array of all prefixes that terminate within its
 * stride window. The best-match pointer cached on each node accelerates
 * the common fast path: a single atomic load reveals the longest covering
 * prefix in the entire subtree if the caller only needs that result.
 *
 * Children are stored in one of two modes that switch automatically at a
 * threshold of @c SPARSE_THRESHOLD:
 *  - **Sparse**: a small sorted array of `(index, child)` slots.
 *  - **Dense**: a bitmap plus a packed heap-allocated child pointer array.
 *    Promotion from sparse to dense is one-way; demotion back to sparse
 *    happens only when the last child is removed.
 *
 * ## Architectural Role
 * LPCTrie is the primary forwarding lookup structure used by the FIB for both
 * IPv4 (N=4, T=NextHopEntry) and IPv6 (N=16). It is faster than
 * @ref RadixTree for read-heavy workloads because its stride reduces the
 * average number of pointer chases per lookup.
 *
 * ## Lifecycle & Ownership
 * Constructed empty. @c insert takes a raw pointer @c T* — the caller retains
 * ownership of the pointed-to object. @c clear destroys all nodes but does
 * not free the objects pointed to by @c T*. @c erase unlinks a prefix entry
 * and prunes empty nodes; again, the pointed-to @c T is not deleted.
 *
 * ## Concurrency Model
 * Single writer, multiple concurrent readers. When @c useRCU is @c true,
 * retired nodes are deferred through @ref utils::RCU::retire so that readers
 * holding a read-side critical section never observe freed node memory.
 * The @c best atomic on each node is updated with release semantics so that
 * readers always see a valid (possibly stale) best-match pointer.
 *
 * @warning Only one thread may call @c insert, @c erase, or @c clear at a
 *          time. Concurrent writes corrupt the child-pointer arrays.
 *
 * @tparam N       Address width in bytes. Must be >= 1.
 *                 Use 4 for IPv4, 16 for IPv6.
 * @tparam T       Value type. The trie stores @c T* pointers; the caller owns
 *                 the pointed-to objects. Must be a complete type at instantiation.
 * @tparam S       Stride in bits per trie level. Must be in [1, 8].
 *                 Higher values reduce lookup depth but increase per-node size.
 *                 Defaults to 8 (256-way fanout per level).
 * @tparam useRCU  When @c true, node reclamation is deferred through
 *                 @ref utils::RCU::retire. When @c false, nodes are deleted
 *                 immediately and the caller must ensure no readers are active.
 *
 * @see RadixTree
 * @see NetworkSpan
 */
template<uint8_t N, typename T, uint8_t S = 8, bool useRCU = false>
class LPCTrie
{
    static_assert(N > 0,            "N must be >= 1");
    static_assert(S >= 1 && S <= 8, "stride S must be 1..8 bits");

public:
    /// Total addressable bits (N * 8).
    static constexpr uint16_t W      = static_cast<uint16_t>(N) * 8;
    /// Number of children per stride level (2^S).
    static constexpr uint8_t  FANOUT = static_cast<uint8_t>(1u << S);
    /// Bitmask for extracting a stride-width index.
    static constexpr uint8_t  SMASK  = static_cast<uint8_t>(FANOUT - 1);

    /// Maximum number of children stored in the sparse child array before
    /// promoting to the dense bitmap representation.
    static constexpr uint8_t  SPARSE_THRESHOLD = 6;

    /**
     * @brief One entry in a node's per-prefix sorted list.
     * @ingroup TYPES
     *
     * Sorted first by length (shorter prefixes sort earlier) then
     * lexicographically by prefix bytes. The sort order is exploited by
     * @c localBest(), which reads the last element to obtain the longest
     * prefix stored on the node.
     */
    struct PrefixEntry
    {
        uint8_t prefix[N];
        uint8_t len;        ///< Prefix length in bits.
        T*      ptr;        ///< Caller-owned value pointer; not freed by the trie.

        bool operator<(const PrefixEntry& o) const noexcept
        {
            if (len != o.len) return len < o.len;
            return std::memcmp(prefix, o.prefix, N) < 0;
        }
    };

    struct Node;

    /**
     * @brief One slot in a sparse child array, pairing a stride-bit index with
     * @ingroup TYPES
     *        the corresponding child node pointer.
     */
    struct ChildSlot
    {
        uint8_t  index;     ///< Stride-bit index in [0, FANOUT).
        uint8_t  _pad[7];
        Node*    child;
    };

    /**
     * @brief A single node in the LC-Trie.
     * @ingroup TYPES
     *
     * Stores zero or more prefix entries that terminate within this node's
     * stride window, an optional path-compression skip (collapsed single-child
     * levels), and child pointers in either sparse or dense form.
     *
     * ## Prefix storage
     * Up to @c PREFIX_INLINE entries are stored inline without a heap
     * allocation; once that threshold is exceeded a heap array is allocated.
     * Entries remain sorted by @c PrefixEntry::operator< at all times.
     *
     * ## Child storage
     * Starts sparse (sorted @c ChildSlot array). Promotes to dense (bitmap +
     * packed pointer array) automatically when @c nChildren exceeds
     * @c SPARSE_THRESHOLD. Demotes back to sparse only when the last dense
     * child is removed.
     */
    struct Node
    {
        uint8_t  skipBits[N];   ///< Reference bytes used during path-compression matching.
        uint8_t  skipLen;       ///< Number of consecutive bits compressed by this node's skip.
        uint8_t  depth;         ///< Depth of this node measured in complete strides from root.

        /// Cached pointer to the locally best (longest) prefix value; updated on
        /// every insert/erase with release semantics for lock-free readers.
        std::atomic<T*> best{nullptr};

        /// Maximum number of PrefixEntry objects stored inline without heap allocation.
        static constexpr uint8_t PREFIX_INLINE = 4;
        uint8_t        nPrefixes{0};
        PrefixEntry*   prefixes{nullptr};   ///< Heap array used when nPrefixes > PREFIX_INLINE; null otherwise.
        PrefixEntry    inlinePfx[PREFIX_INLINE];

        bool     dense{false};  ///< True when the child store is in dense (bitmap) mode.
        uint8_t  nChildren{0};

        /**
         * @brief Child pointer storage as a discriminated union between sparse and dense modes.
         *
         * The sparse variant holds up to @c SPARSE_THRESHOLD sorted @c ChildSlot entries.
         * The dense variant holds a 64-bit bitmap (one bit per possible stride index) plus
         * a packed heap-allocated array of the non-null child pointers in bitmap order.
         */
        union ChildStore
        {
            struct Sparse
            {
                ChildSlot slots[SPARSE_THRESHOLD];
            } sparse;

            struct Dense
            {
                uint64_t  bitmap;
                Node**    children;     ///< Packed array, heap-allocated; length == popcount(bitmap).
            } dense;

            ChildStore() { std::memset(this, 0, sizeof(*this)); }
        } cs;

        Node() : skipLen(0), depth(0)
        {
            std::memset(skipBits, 0, N);
        }

        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;

        PrefixEntry* prefixBegin() noexcept
        {
            return prefixes ? prefixes : inlinePfx;
        }
        const PrefixEntry* prefixBegin() const noexcept
        {
            return prefixes ? prefixes : inlinePfx;
        }
        PrefixEntry* prefixEnd()             noexcept { return prefixBegin() + nPrefixes; }
        const PrefixEntry* prefixEnd() const noexcept { return prefixBegin() + nPrefixes; }

        /**
         * @brief Searches the node's prefix list for an exact match on @p pfx / @p len.
         *
         * @return Pointer to the matching @c PrefixEntry, or @c nullptr if not found.
         */
        PrefixEntry* findPrefix(const uint8_t* pfx, uint8_t len) noexcept
        {
            PrefixEntry* b = prefixBegin();
            PrefixEntry* e = prefixEnd();
            for (PrefixEntry* p = b; p != e; ++p)
                if (p->len == len && std::memcmp(p->prefix, pfx, N) == 0)
                    return p;
            return nullptr;
        }

        /**
         * @brief Inserts or updates the prefix entry for @p pfx / @p len on this node.
         *
         * Promotes from inline to heap storage when the inline capacity is exhausted.
         * The prefix list is kept sorted after every modification.
         *
         * @param pfx Pointer to N prefix bytes (caller-masked).
         * @param len Prefix length in bits.
         * @param ptr Caller-owned value pointer to store.
         */
        void insertPrefix(const uint8_t* pfx, uint8_t len, T* ptr)
        {
            // Check for existing entry
            PrefixEntry* existing = findPrefix(pfx, len);
            if (existing) { existing->ptr = ptr; return; }

            PrefixEntry entry;
            std::memcpy(entry.prefix, pfx, N);
            entry.len = len;
            entry.ptr = ptr;

            if (nPrefixes < PREFIX_INLINE && !prefixes)
            {
                // Still in inline storage
                inlinePfx[nPrefixes++] = entry;
                std::sort(prefixBegin(), prefixEnd());
                return;
            }

            uint8_t newCount = nPrefixes + 1;
            PrefixEntry* newArr = new PrefixEntry[newCount];
            std::memcpy(newArr, prefixBegin(), nPrefixes * sizeof(PrefixEntry));
            newArr[nPrefixes] = entry;
            std::sort(newArr, newArr + newCount);
            delete[] prefixes;
            prefixes = newArr;
            nPrefixes = newCount;
        }

        /**
         * @brief Removes the prefix entry for @p pfx / @p len from this node.
         *
         * @return @c true if the entry was found and removed, @c false if not present.
         */
        bool removePrefix(const uint8_t* pfx, uint8_t len)
        {
            PrefixEntry* b = prefixBegin();
            PrefixEntry* e = prefixEnd();
            for (PrefixEntry* p = b; p != e; ++p)
            {
                if (p->len == len && std::memcmp(p->prefix, pfx, N) == 0)
                {
                    // Shift remaining entries down
                    std::memmove(p, p + 1, (e - p - 1) * sizeof(PrefixEntry));
                    --nPrefixes;
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Returns the value pointer of the longest prefix stored on this node.
         *
         * Exploits the sorted order: the last entry always has the greatest length.
         * Returns @c nullptr if no prefixes are stored.
         */
        T* localBest() const noexcept
        {
            if (!nPrefixes) return nullptr;
            return prefixEnd()[-1].ptr;
        }

        /**
         * @brief Retrieves the child node for stride index @p idx, or @c nullptr if absent.
         *
         * Dispatches to the sparse linear search or the dense bitmap + popcount
         * path depending on the current child storage mode.
         */
        Node* getChild(uint8_t idx) const noexcept
        {
            if (!dense)
            {
                const ChildSlot* b = cs.sparse.slots;
                const ChildSlot* e = b + nChildren;
                const ChildSlot* it = std::lower_bound(b, e, idx,
                    [](const ChildSlot& s, uint8_t v){ return s.index < v; });
                if (it != e && it->index == idx)
                    return it->child;
                return nullptr;
            }
            else
            {
                uint64_t bit = uint64_t(1) << idx;
                if (!(cs.dense.bitmap & bit)) return nullptr;
                uint8_t pos = static_cast<uint8_t>(
                    __builtin_popcountll(cs.dense.bitmap & (bit - 1)));
                return cs.dense.children[pos];
            }
        }

        /**
         * @brief Sets the child pointer for stride index @p idx.
         *
         * In sparse mode, inserts a new sorted slot. Promotes to dense mode
         * automatically when the sparse threshold is reached.
         * In dense mode, either updates an existing slot or inserts a new one
         * into the packed array.
         *
         * @param idx   Stride-bit index in [0, FANOUT).
         * @param child Pointer to the child node.
         */
        void setChild(uint8_t idx, Node* child)
        {
            if (!dense)
            {
                ChildSlot* b = cs.sparse.slots;
                ChildSlot* e = b + nChildren;
                ChildSlot* it = std::lower_bound(b, e, idx,
                    [](const ChildSlot& s, uint8_t v){ return s.index < v; });

                if (it != e && it->index == idx)
                {
                    it->child = child;
                    return;
                }

                if (nChildren < SPARSE_THRESHOLD)
                {
                    // Shift right and insert
                    std::memmove(it + 1, it,
                        (e - it) * sizeof(ChildSlot));
                    it->index = idx;
                    it->child = child;
                    ++nChildren;
                    return;
                }

                // Promote to dense
                promoteTodense(idx, child);
                return;
            }

            uint64_t bit = uint64_t(1) << idx;
            if (cs.dense.bitmap & bit)
            {
                uint8_t pos = static_cast<uint8_t>(
                    __builtin_popcountll(cs.dense.bitmap & (bit - 1)));
                cs.dense.children[pos] = child;
                return;
            }

            uint8_t pos = static_cast<uint8_t>(
                __builtin_popcountll(cs.dense.bitmap & (bit - 1)));
            uint8_t newCount = nChildren + 1;
            Node** newArr = new Node*[newCount];
            std::memcpy(newArr,         cs.dense.children, pos * sizeof(Node*));
            newArr[pos] = child;
            std::memcpy(newArr + pos + 1, cs.dense.children + pos,
                (nChildren - pos) * sizeof(Node*));
            delete[] cs.dense.children;
            cs.dense.children = newArr;
            cs.dense.bitmap  |= bit;
            ++nChildren;
        }

        /**
         * @brief Unlinks and returns the child at stride index @p idx.
         *
         * Demotes from dense to sparse (resetting the bitmap to zero) when
         * the last dense child is removed.
         *
         * @param idx Stride-bit index in [0, FANOUT).
         * @return Pointer to the removed child node, or @c nullptr if not present.
         */
        Node* removeChild(uint8_t idx)
        {
            if (!dense)
            {
                ChildSlot* b = cs.sparse.slots;
                ChildSlot* e = b + nChildren;
                ChildSlot* it = std::lower_bound(b, e, idx,
                    [](const ChildSlot& s, uint8_t v){ return s.index < v; });
                if (it == e || it->index != idx) return nullptr;
                Node* removed = it->child;
                std::memmove(it, it + 1, (e - it - 1) * sizeof(ChildSlot));
                --nChildren;
                return removed;
            }

            uint64_t bit = uint64_t(1) << idx;
            if (!(cs.dense.bitmap & bit)) return nullptr;

            uint8_t pos = static_cast<uint8_t>(
                __builtin_popcountll(cs.dense.bitmap & (bit - 1)));
            Node* removed = cs.dense.children[pos];

            uint8_t newCount = nChildren - 1;
            if (newCount == 0)
            {
                delete[] cs.dense.children;
                cs.dense.children = nullptr;
                cs.dense.bitmap   = 0;
                dense = false;
            }
            else
            {
                Node** newArr = new Node*[newCount];
                std::memcpy(newArr, cs.dense.children, pos * sizeof(Node*));
                std::memcpy(newArr + pos, cs.dense.children + pos + 1,
                    (nChildren - pos - 1) * sizeof(Node*));
                delete[] cs.dense.children;
                cs.dense.children = newArr;
                cs.dense.bitmap  &= ~bit;
            }
            --nChildren;
            return removed;
        }

        ~Node()
        {
            delete[] prefixes;
            if (dense) delete[] cs.dense.children;
        }

    private:
        /**
         * @brief Promotes the child store from sparse to dense mode, inserting
         *        @p newChild at index @p newIdx in the same operation.
         *
         * Builds the full bitmap from existing sparse slots plus the new entry,
         * allocates the packed child array in bitmap order, and switches
         * @c dense to @c true. Called only when the sparse threshold is reached.
         */
        void promoteTodense(uint8_t newIdx, Node* newChild)
        {
            uint64_t bmp = uint64_t(1) << newIdx;
            for (uint8_t i = 0; i < nChildren; ++i)
                bmp |= uint64_t(1) << cs.sparse.slots[i].index;

            uint8_t total = static_cast<uint8_t>(__builtin_popcountll(bmp));
            Node** arr = new Node*[total];

            uint64_t tmp = bmp;
            uint8_t  pos = 0;
            while (tmp)
            {
                uint8_t bit = static_cast<uint8_t>(__builtin_ctzll(tmp));
                if (bit == newIdx)
                    arr[pos] = newChild;
                else
                {
                    for (uint8_t i = 0; i < nChildren; ++i)
                        if (cs.sparse.slots[i].index == bit)
                            { arr[pos] = cs.sparse.slots[i].child; break; }
                }
                ++pos;
                tmp &= tmp - 1;
            }

            cs.dense.bitmap   = bmp;
            cs.dense.children = arr;
            dense             = true;
            nChildren         = total;
        }
    };

    /**
     * @brief Visits every prefix entry in the trie, invoking @p fn for each one.
     *
     * Traversal order is unspecified. The callback receives the raw prefix bytes,
     * the prefix length in bits, and the stored @c T* pointer.
     *
     * @tparam F Callable compatible with @c void(const uint8_t*, uint8_t, T*).
     * @param fn Visitor function invoked once per @c PrefixEntry across all nodes.
     */
    template <typename F>
    void forEach(F&& fn) const noexcept
    {
        const Node* r = root.load(std::memory_order_acquire);
        if (r) forEachNode(r, std::forward<F>(fn));
    }

    /**
     * @brief Finds the longest prefix that covers @p addr.
     *
     * Descends the trie consuming @c S bits per level, verifying path-
     * compression skip bits along the way. At each node all stored prefix
     * entries are checked for coverage; the last matching one (longest) is
     * returned on success.
     *
     * @tparam AddrT Unsigned integer type whose byte width equals @c N
     *               (e.g. @c uint32_t for IPv4, @c unsigned __int128 for IPv6).
     *               Accessed via a @ref NetworkSpan to hide endianness.
     * @param addr Network-order address wrapped in a @ref NetworkSpan.
     * @return Pointer to the best-match value, or @c nullptr if no prefix covers @p addr.
     *
     * @note Safe to call concurrently when @c useRCU is @c true and the caller
     *       holds an RCU read guard.
     */
    template<std::unsigned_integral AddrT>
    T* lookup(const NetworkSpan<AddrT>& addr) const noexcept
    {
        static_assert(sizeof(AddrT) == N, "address type size must match trie byte width N");
        Node* n    = root.load(std::memory_order_acquire);
        T*    best = nullptr;
        uint16_t pos = 0;   // current bit position in addr

        while (n)
        {
            // Path compression check: verify skipped bits match
            if (n->skipLen > 0)
            {
                if (!bitsMatch(addr, n->skipBits, pos, n->skipLen))
                    break;
                pos += n->skipLen;
            }

            // Check all prefix entries on this node for coverage
            const PrefixEntry* pb = n->prefixBegin();
            const PrefixEntry* pe = n->prefixEnd();
            for (const PrefixEntry* p = pb; p != pe; ++p)
            {
                if (prefixCovers(addr, p->prefix, p->len))
                    best = p->ptr;
            }

            if (pos >= W) break;

            uint8_t idx = extractBits(addr, pos, S);
            pos += S;

            Node* child = n->getChild(idx);
            n = child;
        }

        return best;
    }

    /**
     * @brief Finds the value pointer for the exact prefix @p pfx / @p len.
     *
     * Unlike @c lookup, covering prefixes do not satisfy the query — only an
     * exact length and byte match returns a result.
     *
     * @param pfx Pointer to N prefix bytes.
     * @param len Prefix length in bits.
     * @return Stored @c T* pointer, or @c nullptr if not found.
     */
    T* lookupExact(const uint8_t* pfx, uint8_t len) const noexcept
    {
        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* n   = root.load(std::memory_order_acquire);
        uint16_t pos = 0;

        while (n)
        {
            if (n->skipLen > 0)
            {
                if (!bitsMatch(masked, n->skipBits, pos, n->skipLen))
                    return nullptr;
                pos += n->skipLen;
            }

            PrefixEntry* e = n->findPrefix(masked, len);
            if (e) return e->ptr;

            if (pos >= W) break;

            uint8_t idx = extractBits(masked, pos, S);
            pos += S;
            n = n->getChild(idx);
        }
        return nullptr;
    }

    /**
     * @brief Inserts a new prefix/pointer pair into the trie.
     *
     * The host bits of @p pfx beyond @p len are masked to zero. If the prefix
     * already exists its stored pointer is updated. A new root node is created
     * if the trie is empty.
     *
     * @param pfx   Pointer to N prefix bytes.
     * @param len   Prefix length in bits [0, W].
     * @param entry Caller-owned value pointer to associate with this prefix.
     * @return @c true on success; @c false if @p len > W.
     *
     * @warning Must not be called concurrently with any other mutating operation.
     */
    bool insert(const uint8_t* pfx, uint8_t len, T* entry)
    {
        if (len > W) return false;

        uint8_t masked[N];
        applyMask(pfx, len, masked);

        // Ensure root
        if (!root.load(std::memory_order_acquire))
        {
            Node* fresh = new Node();
            Node* expected = nullptr;
            if (!root.compare_exchange_strong(expected, fresh,
                    std::memory_order_release, std::memory_order_acquire))
                delete fresh;
        }

        insertAt(root.load(std::memory_order_acquire), masked, len, entry, 0);
        return true;
    }

    /**
     * @brief Removes the prefix entry for @p pfx / @p len from the trie.
     *
     * After removing the entry, empty leaf nodes are pruned. The @c T* value
     * pointed to by the entry is not freed.
     *
     * @param pfx Pointer to N prefix bytes.
     * @param len Prefix length in bits.
     * @return @c true if the prefix was found and removed, @c false otherwise.
     *
     * @warning Must not be called concurrently with any other mutating operation.
     */
    bool erase(const uint8_t* pfx, uint8_t len)
    {
        if (len > W) return false;

        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* r = root.load(std::memory_order_acquire);
        if (!r) return false;

        return eraseAt(r, nullptr, 0, masked, len, 0);
    }

    /**
     * @brief Removes all nodes from the trie, resetting it to an empty state.
     *
     * Node memory is reclaimed according to the @c useRCU policy. Pointed-to
     * @c T objects are not freed.
     *
     * @warning Must not be called concurrently with any other operation.
     */
    void clear()
    {
        Node* old = root.exchange(nullptr, std::memory_order_acq_rel);
        destroyAll(old);
    }

    /**
     * @brief Extracts @p count consecutive bits from @p addr starting at bit offset @p off.
     *
     * Bits are in network (big-endian) order: bit 0 is the MSB of @p addr[0].
     * The extracted bits are returned right-aligned in the result byte.
     *
     * @param addr  Pointer to N address bytes.
     * @param off   Starting bit index [0, W).
     * @param count Number of bits to extract [1, 8].
     * @return Extracted bits in the low @p count bits of the result.
     */
    static uint8_t extractBits(const uint8_t* addr, uint16_t off, uint8_t count) noexcept
    {
        uint8_t result = 0;
        for (uint8_t i = 0; i < count; ++i)
        {
            uint16_t bitIdx = off + i;
            uint8_t  b      = (addr[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
            result = static_cast<uint8_t>((result << 1) | b);
        }
        return result;
    }

    /**
     * @brief @ref extractBits overload for @ref NetworkSpan-wrapped addresses.
     *
     * @tparam AddrT Unsigned integer type whose byte width equals @c N.
     * @param addr  Network-order address wrapped in a @ref NetworkSpan.
     * @param off   Starting bit index [0, W).
     * @param count Number of bits to extract [1, 8].
     * @return Extracted bits in the low @p count bits of the result.
     */
    template<std::unsigned_integral AddrT>
    static uint8_t extractBits(const NetworkSpan<AddrT>& addr, uint16_t off, uint8_t count) noexcept
    {
        uint8_t result = 0;
        for (uint8_t i = 0; i < count; ++i)
        {
            uint16_t bitIdx = off + i;
            uint8_t  b      = (addr[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
            result = static_cast<uint8_t>((result << 1) | b);
        }
        return result;
    }

    /**
     * @brief Masks the host bits of @p pfx beyond @p len, writing the result to @p out.
     *
     * Bits [len, W) in @p out are zeroed. This canonicalises prefixes so that
     * 10.1.2.3/24 and 10.1.2.0/24 map to the same key.
     *
     * @param pfx Pointer to N input bytes.
     * @param len Number of prefix bits to preserve [0, W].
     * @param out Pointer to N output bytes. May not alias @p pfx.
     */
    static void applyMask(const uint8_t* pfx, uint8_t len, uint8_t* out) noexcept
    {
        if (len == 0) { std::memset(out, 0, N); return; }
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
    static void forEachNode(const Node* n, F&& fn) noexcept
    {
        const PrefixEntry* pb = n->prefixBegin();
        const PrefixEntry* pe = n->prefixEnd();
        for (const PrefixEntry* p = pb; p != pe; ++p)
            fn(p->prefix, p->len, p->ptr);

        if (!n->dense)
        {
            for (uint8_t i = 0; i < n->nChildren; ++i)
                forEachNode(n->cs.sparse.slots[i].child, fn);
        }
        else
        {
            uint8_t count = static_cast<uint8_t>(__builtin_popcountll(n->cs.dense.bitmap));
            for (uint8_t i = 0; i < count; ++i)
                forEachNode(n->cs.dense.children[i], fn);
        }
    }

    /**
     * @brief Returns true if @p count bits of @p addr starting at offset @p off
     *        match the corresponding bits of @p ref.
     *
     * Used to validate path-compression skip bits during descent.
     */
    static bool bitsMatch(const uint8_t* addr, const uint8_t* ref,
                          uint16_t off, uint8_t count) noexcept
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            uint16_t bitIdx = off + i;
            uint8_t  a = (addr[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
            uint8_t  r = (ref [bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
            if (a != r) return false;
        }
        return true;
    }

    /// @ref bitsMatch overload for @ref NetworkSpan-wrapped addresses.
    template<std::unsigned_integral AddrT>
    static bool bitsMatch(const NetworkSpan<AddrT>& addr, const uint8_t* ref,
                          uint16_t off, uint8_t count) noexcept
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            uint16_t bitIdx = off + i;
            uint8_t  a = (addr[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
            uint8_t  r = (ref [bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
            if (a != r) return false;
        }
        return true;
    }

    /// Returns true if @p prefix/len is a covering prefix of @p addr (raw byte array variant).
    static bool prefixCovers(const uint8_t* addr,
                              const uint8_t* prefix,
                              uint8_t        len) noexcept
    {
        if (len == 0) return true;
        uint8_t full = len >> 3;
        uint8_t rem  = len & 7;
        if (full && std::memcmp(addr, prefix, full) != 0) return false;
        if (rem)
        {
            uint8_t m = static_cast<uint8_t>(0xFF00u >> rem);
            if ((addr[full] & m) != prefix[full]) return false;
        }
        return true;
    }

    /// @ref prefixCovers overload for @ref NetworkSpan-wrapped addresses.
    template<std::unsigned_integral AddrT>
    static bool prefixCovers(const NetworkSpan<AddrT>& addr,
                              const uint8_t* prefix,
                              uint8_t        len) noexcept
    {
        if (len == 0) return true;
        uint8_t full = len >> 3;
        uint8_t rem  = len & 7;
        for (uint8_t i = 0; i < full; ++i)
            if (addr[i] != prefix[i]) return false;
        if (rem)
        {
            uint8_t m = static_cast<uint8_t>(0xFF00u >> rem);
            if ((addr[full] & m) != prefix[full]) return false;
        }
        return true;
    }

    /**
     * @brief Recursive insertion into the subtree rooted at @p n, starting at bit @p pos.
     *
     * Consumes the skip bits stored on @p n, then either stores the prefix on
     * @p n (if remaining bits fit within one stride) or descends to a child,
     * creating one if absent. When path compression diverges, @c splitNode is
     * called to introduce a branch node at the divergence point.
     *
     * @param n     Current node.
     * @param pfx   Canonicalised prefix bytes.
     * @param len   Prefix length in bits.
     * @param entry Caller-owned value pointer.
     * @param pos   Current bit offset (consumed bits so far).
     */
    void insertAt(Node* n, const uint8_t* pfx, uint8_t len, T* entry, uint16_t pos)
    {
        // Consume skip bits
        if (n->skipLen > 0)
        {
            uint8_t matchLen = commonPrefixLen(pfx, n->skipBits, pos,
                                               pos + n->skipLen);
            if (matchLen < n->skipLen)
            {
                // Split: create a new branch node at the divergence point
                splitNode(n, pfx, len, entry, pos, matchLen);
                return;
            }
            pos += n->skipLen;
        }

        // If the prefix ends at or before this stride, store it here
        if (pos >= len)
        {
            n->insertPrefix(pfx, len, entry);
            updateBest(n, entry);
            return;
        }

        uint16_t remaining = len - pos;

        // If remaining bits fit within one stride, store on this node
        if (remaining <= S)
        {
            n->insertPrefix(pfx, len, entry);
            updateBest(n, entry);
            return;
        }

        // Descend
        uint8_t idx   = extractBits(pfx, pos, S);
        pos += S;
        Node*   child = n->getChild(idx);

        if (!child)
        {
            // Create new leaf node with path compression
            Node* leaf    = new Node();
            leaf->depth   = static_cast<uint8_t>(pos / S);

            if (len > pos)
            {
                leaf->skipLen = static_cast<uint8_t>(len - pos);
                std::memcpy(leaf->skipBits, pfx, N);
            }

            leaf->insertPrefix(pfx, len, entry);
            leaf->best.store(entry, std::memory_order_relaxed);
            n->setChild(idx, leaf);
            updateBest(n, entry);
            return;
        }

        insertAt(child, pfx, len, entry, pos);
        updateBest(n, child->best.load(std::memory_order_relaxed));
    }

    /**
     * @brief Splits @p n at bit position @p pos + @p matchLen to introduce a branch
     *        for the new prefix @p pfx / @p len.
     *
     * The current node's skip is shortened to @p matchLen bits; the remainder
     * of the old skip and @p n's existing children/prefixes are moved to a new
     * sub-node linked as a child of @p n at the old diverging stride index.
     * The new prefix is then inserted via @c insertAt starting from the branch point.
     *
     * @param n        Node whose skip compression diverges from the new prefix.
     * @param pfx      New prefix bytes.
     * @param len      New prefix length.
     * @param entry    New value pointer.
     * @param pos      Bit offset at which @p n's skip begins.
     * @param matchLen Number of skip bits that match before the divergence.
     */
    void splitNode(Node* n, const uint8_t* pfx, uint8_t len, T* entry,
                   uint16_t pos, uint8_t matchLen)
    {
        uint8_t oldSkipLen = n->skipLen;
        n->skipLen = matchLen;
        pos += matchLen;

        // The diverging bit of the old skip becomes a child index
        if (pos + S <= W)
        {
            uint8_t oldIdx = extractBits(n->skipBits, pos, S);
            pos += S;

            // Move n's existing children and prefixes to a new sub-node
            Node* sub       = new Node();
            sub->depth      = static_cast<uint8_t>(pos / S);
            sub->skipLen   = static_cast<uint8_t>(oldSkipLen - matchLen - S > 0
                                  ? oldSkipLen - matchLen - S : 0);
            if (sub->skipLen > 0)
                std::memcpy(sub->skipBits, n->skipBits, N);

            // Move children from n to sub
            if (!n->dense)
            {
                for (uint8_t i = 0; i < n->nChildren; ++i)
                    sub->setChild(n->cs.sparse.slots[i].index,
                                  n->cs.sparse.slots[i].child);
            }
            else
            {
                uint64_t bmp = n->cs.dense.bitmap;
                uint8_t  pi  = 0;
                while (bmp)
                {
                    uint8_t bit = static_cast<uint8_t>(__builtin_ctzll(bmp));
                    sub->setChild(bit, n->cs.dense.children[pi++]);
                    bmp &= bmp - 1;
                }
                delete[] n->cs.dense.children;
                n->cs.dense.children = nullptr;
                n->cs.dense.bitmap   = 0;
                n->dense             = false;
            }
            n->nChildren = 0;

            PrefixEntry* pb = n->prefixBegin();
            PrefixEntry* pe = n->prefixEnd();
            for (PrefixEntry* p = pb; p != pe; ++p)
                sub->insertPrefix(p->prefix, p->len, p->ptr);
            delete[] n->prefixes;
            n->prefixes   = nullptr;
            n->nPrefixes  = 0;

            n->setChild(oldIdx, sub);
        }

        // Now insert the new prefix from the current pos
        insertAt(n, pfx, len, entry, pos);
    }

    /**
     * @brief Returns the number of consecutive identical bits between @p a and @p b
     *        in the range [@p off, @p end).
     */
    static uint8_t commonPrefixLen(const uint8_t* a, const uint8_t* b,
                                   uint16_t off, uint16_t end) noexcept
    {
        uint8_t count = 0;
        for (uint16_t i = off; i < end; ++i)
        {
            uint8_t ba = (a[i >> 3] >> (7 - (i & 7))) & 1;
            uint8_t bb = (b[i >> 3] >> (7 - (i & 7))) & 1;
            if (ba != bb) break;
            ++count;
        }
        return count;
    }

    /// Updates @p n's cached best-match pointer if @p candidate is non-null.
    static void updateBest(Node* n, T* candidate) noexcept
    {
        if (candidate)
            n->best.store(candidate, std::memory_order_release);
    }

    /**
     * @brief Recursive erasure starting at node @p n.
     *
     * Verifies skip bits, then tries to remove the prefix from @p n's prefix
     * list. If found and the node becomes empty and childless, the node is
     * detached from @p parent and retired. If not found on this node, descends
     * to the child indicated by the next stride.
     *
     * @return @c true if the prefix was found and removed.
     */
    bool eraseAt(Node* n, Node* parent, uint8_t parentIdx,
                 const uint8_t* pfx, uint8_t len, uint16_t pos)
    {
        if (n->skipLen > 0)
        {
            if (!bitsMatch(pfx, n->skipBits, pos, n->skipLen))
                return false;
            pos += n->skipLen;
        }

        // Try removing from this node's prefix list first
        if (n->removePrefix(pfx, len))
        {
            // Update best hint
            n->best.store(n->localBest(), std::memory_order_release);

            // Prune if node is now empty and childless
            if (n->nPrefixes == 0 && n->nChildren == 0 && parent)
            {
                parent->removeChild(parentIdx);
                retireNode(n);
            }
            return true;
        }

        if (pos >= W) return false;

        uint8_t idx   = extractBits(pfx, pos, S);
        pos += S;
        Node*   child = n->getChild(idx);
        if (!child) return false;

        bool removed = eraseAt(child, n, idx, pfx, len, pos);
        if (removed)
            n->best.store(n->localBest(), std::memory_order_release);
        return removed;
    }

    /// Post-order traversal that retires every node in the subtree rooted at @p n.
    static void destroyAll(Node* n)
    {
        if (!n) return;

        if (!n->dense)
        {
            for (uint8_t i = 0; i < n->nChildren; ++i)
                destroyAll(n->cs.sparse.slots[i].child);
        }
        else
        {
            uint8_t count = static_cast<uint8_t>(
                __builtin_popcountll(n->cs.dense.bitmap));
            for (uint8_t i = 0; i < count; ++i)
                destroyAll(n->cs.dense.children[i]);
        }

        retireNode(n);
    }

    static void destroyNode(Node* n) { delete n; }

    /**
     * @brief Reclaims @p n according to the @c useRCU policy.
     *
     * With @c useRCU=true the deletion is deferred via @ref utils::RCU::retire
     * so readers in a read-side critical section see valid memory until they exit.
     * With @c useRCU=false the node is deleted immediately.
     */
    static void retireNode(Node* n)
    {
        if (!n) return;
        if constexpr (useRCU)
            utils::RCU::retire([n]{ destroyNode(n); });
        else
            destroyNode(n);
    }

    std::atomic<Node*> root{nullptr}; ///< Root of the LC-Trie; null when empty.
};

} // namespace types
