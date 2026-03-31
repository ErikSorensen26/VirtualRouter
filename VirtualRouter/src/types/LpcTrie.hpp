/**
 * @file LpcTrie.hpp
 * @brief Level-Compressed Patricia Trie for high-performance IP longest-prefix-match.
 */

// // TODO finish doxy

#ifndef LPC_TRIE_HPP
#define LPC_TRIE_HPP

#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <RCU.hpp>
#include <NetworkSpan.hpp>
#include <ByteUtils.hpp>

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
template<uint8_t Nu, typename T, uint8_t S = 4, bool useRCU = false>
class LPCTrie
{
    static_assert(Nu > 0,            "N must be >= 1");
    static_assert(S >= 1 && S <= 8, "stride S must be 1..8");
 
public:
    static constexpr uint8_t N = Nu;

    // The canonical address integer type (uint32_t for IPv4, __uint128_t for IPv6).
    using AddrT  = typename utils::smallestInteger<N>::type;
    using SpanT  = NetworkSpan<AddrT>;
 
    static constexpr uint16_t W      = static_cast<uint16_t>(N) * 8; ///< Total bits.
    static constexpr uint8_t  FANOUT = static_cast<uint8_t>(1u << S); ///< Children per node.
    static constexpr AddrT    SMASK  = static_cast<AddrT>(FANOUT - 1u); ///< Stride mask.
 
    // ── Types ─────────────────────────────────────────────────────────────────
 
    /**
     * One prefix entry stored on a node.
     * Sorted by length then lexicographically so the last entry is always
     * the longest-prefix match (exploited by localBest()).
     */
    struct PrefixEntry
    {
        AddrT   addr;   ///< Host-order prefix, host bits zeroed.
        T*      ptr;
        uint8_t len;    ///< Prefix length in bits [0..W].
 
        bool operator<(const PrefixEntry& o) const noexcept
        {
            return len != o.len ? len < o.len : addr < o.addr;
        }
    };
 
    struct Node;
 
    // Dense child array: FANOUT atomic Node* pointers.
    // For S≤4 (≤16 children) this is 128 bytes — fits in two cache lines.
    // For S=8 (256 children) this is 2 KB — still faster than sparse search.
    struct ChildArray
    {
        std::atomic<Node*> slots[FANOUT]{};
        ChildArray() noexcept = default;
        ChildArray(const ChildArray&) = delete;
        ChildArray& operator=(const ChildArray&) = delete;
    };
 
    /**
     * A trie node.
     *
     * ## Lookup hot path (read-side, lock-free)
     * Only `skipPattern`, `skipMask`, `skipLen`, `best`, and the child array
     * are touched. All are accessed with acquire semantics or relaxed (when the
     * node pointer itself was already acquired).
     *
     * ## Prefix storage
     * Inline array for ≤ PREFIX_INLINE entries; heap array beyond that.
     * Kept sorted at all times.
     *
     * ## Child storage
     * Always dense: `FANOUT` atomic Node* in a heap-allocated ChildArray.
     * Null slots occupy no extra space beyond the pointer itself.
     * For small strides (S=4) this is only 128 bytes per node.
     */
    struct Node
    {
        // ── Hot fields (touched by lookup) ───────────────────────────────────
        AddrT   skipPattern{0}; ///< Compressed bits, host-order, right-aligned.
        AddrT   skipMask   {0}; ///< 1-bits for every compressed bit position.
        uint8_t skipLen    {0}; ///< How many bits are compressed.
 
        /// Cached longest-prefix value for this subtree.
        /// Written with release; read with relaxed (guarded by the node-pointer
        /// acquire on the parent's child slot).
        std::atomic<T*> best{nullptr};
 
        // ── Prefix storage ────────────────────────────────────────────────────
        static constexpr uint8_t PREFIX_INLINE = 8;
        uint8_t       nPrefixes{0};
        PrefixEntry*  heapPfx{nullptr};   ///< non-null iff nPrefixes > PREFIX_INLINE
        PrefixEntry   inlinePfx[PREFIX_INLINE];
 
        // ── Child storage (always dense) ──────────────────────────────────────
        ChildArray* children{nullptr};  ///< Allocated on demand (nullptr = leaf).
 
        // ── Bookkeeping ───────────────────────────────────────────────────────
        uint8_t depth{0};
 
        Node() noexcept = default;
        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;
 
        ~Node()
        {
            delete[] heapPfx;
            delete children;
        }
 
        // ── Prefix accessors ──────────────────────────────────────────────────
        PrefixEntry* prefixBegin() noexcept
            { return heapPfx ? heapPfx : inlinePfx; }
        const PrefixEntry* prefixBegin() const noexcept
            { return heapPfx ? heapPfx : inlinePfx; }
        PrefixEntry* prefixEnd()       noexcept { return prefixBegin() + nPrefixes; }
        const PrefixEntry* prefixEnd() const noexcept { return prefixBegin() + nPrefixes; }
 
        /** O(n) scan — used only on write path. */
        PrefixEntry* findPrefix(AddrT addr, uint8_t len) noexcept
        {
            for (auto* p = prefixBegin(); p != prefixEnd(); ++p)
                if (p->len == len && p->addr == addr) return p;
            return nullptr;
        }
        const PrefixEntry* findPrefix(AddrT addr, uint8_t len) const noexcept
        {
            for (const auto* p = prefixBegin(); p != prefixEnd(); ++p)
                if (p->len == len && p->addr == addr) return p;
            return nullptr;
        }
 
        /**
         * Insert or update. Keeps the list sorted.
         * Promotes to heap storage when inline capacity is exhausted.
         */
        void insertPrefix(AddrT addr, uint8_t len, T* ptr)
        {
            if (PrefixEntry* ex = findPrefix(addr, len))
                { ex->ptr = ptr; return; }
 
            PrefixEntry entry{addr, ptr, len};
 
            if (nPrefixes < PREFIX_INLINE && !heapPfx)
            {
                inlinePfx[nPrefixes++] = entry;
                std::sort(prefixBegin(), prefixEnd());
                return;
            }
 
            // Promote or grow heap array.
            uint8_t newCount = nPrefixes + 1;
            PrefixEntry* arr = new PrefixEntry[newCount];
            std::memcpy(arr, prefixBegin(), nPrefixes * sizeof(PrefixEntry));
            arr[nPrefixes] = entry;
            std::sort(arr, arr + newCount);
            delete[] heapPfx;
            heapPfx   = arr;
            nPrefixes = newCount;
        }
 
        /**
         * Remove an exact match. Returns true if found.
         * Does NOT demote heap→inline after removal (keeps allocator churn low).
         */
        bool removePrefix(AddrT addr, uint8_t len) noexcept
        {
            PrefixEntry* b = prefixBegin();
            PrefixEntry* e = prefixEnd();
            for (PrefixEntry* p = b; p != e; ++p)
            {
                if (p->len == len && p->addr == addr)
                {
                    std::memmove(p, p + 1, static_cast<size_t>(e - p - 1) * sizeof(PrefixEntry));
                    --nPrefixes;
                    return true;
                }
            }
            return false;
        }
 
        /**
         * The longest prefix stored directly on this node, or nullptr.
         * Because the list is sorted by length ascending, the last entry wins.
         */
        T* localBest() const noexcept
        {
            if (!nPrefixes) return nullptr;
            return prefixEnd()[-1].ptr;
        }
 
        /** Recompute best by scanning the subtree breadth-first. */
        T* subtreeBest() const noexcept
        {
            // For the purposes of updateBest we just use localBest — the
            // invariant is maintained bottom-up during insert/erase.
            return localBest();
        }
 
        // ── Child accessors (hot path inlined) ────────────────────────────────
 
        /** Acquire-load of child at stride index idx. */
        __attribute__((always_inline))
        Node* getChild(uint8_t idx) const noexcept
        {
            if (!children) return nullptr;
            return children->slots[idx].load(std::memory_order_acquire);
        }
 
        /** Release-store of child at stride index idx. */
        void setChild(uint8_t idx, Node* child) noexcept
        {
            if (!children)
                children = new ChildArray();
            children->slots[idx].store(child, std::memory_order_release);
        }
 
        /**
         * Atomically exchange child at idx with nullptr.
         * Returns the old pointer.
         */
        Node* removeChild(uint8_t idx) noexcept
        {
            if (!children) return nullptr;
            return children->slots[idx].exchange(nullptr, std::memory_order_acq_rel);
        }
 
        bool isEmpty() const noexcept
        {
            if (nPrefixes) return false;
            if (!children)  return true;
            for (uint8_t i = 0; i < FANOUT; ++i)
                if (children->slots[i].load(std::memory_order_relaxed))
                    return false;
            return true;
        }
    };
 
    // ── Public interface ──────────────────────────────────────────────────────
 
    LPCTrie()  noexcept = default;
    ~LPCTrie() noexcept { clear(); }
 
    LPCTrie(const LPCTrie&)            = delete;
    LPCTrie& operator=(const LPCTrie&) = delete;
 
    /**
     * @brief Longest-prefix lookup — the hot path.
     *
     * Algorithm:
     *  1. Load root with acquire.
     *  2. At each node, validate skip bits with a single XOR+AND.
     *  3. Read n->best (relaxed — guarded by the pointer acquire).
     *  4. Extract the next S-bit stride and descend.
     *
     * Zero byte-array operations. Zero heap allocations. O(W/S) iterations.
     *
     * @param addr  Host-order address (from NetworkSpan implicit conversion).
     * @return      Longest matching prefix value, or nullptr.
     */
    __attribute__((hot))
    T* lookup(const SpanT& span) const noexcept
    {
        const AddrT addr = static_cast<AddrT>(span);
 
        const Node* n    = root_.load(std::memory_order_acquire);
        T*          best = nullptr;
        uint16_t    pos  = 0;
 
        while (n) [[likely]]
        {
            if (n->skipLen)
            {
                if (((addr ^ n->skipPattern) & n->skipMask) != 0) [[unlikely]]
                    break;
                pos += n->skipLen;
            }
 
            if (T* v = n->best.load(std::memory_order_relaxed))
                best = v;
 
            if (pos >= W) [[unlikely]]
                break;
 
            const uint16_t shift = static_cast<uint16_t>(W - pos - S);
            const uint8_t  idx   = static_cast<uint8_t>((addr >> shift) & SMASK);
            pos += S;
 
            if (!n->children) break;
            n = n->children->slots[idx].load(std::memory_order_acquire);
        }
 
        return best;
    }
 
    /**
     * Exact-match lookup: only returns a value if both the prefix bytes and
     * the prefix length match precisely.
     */
    T* lookupExact(const uint8_t* pfx, uint8_t len) const noexcept
    {
        if (len > W) return nullptr;
        const AddrT masked = applyMask(toHostOrder(pfx), len);
 
        const Node* n   = root_.load(std::memory_order_acquire);
        uint16_t    pos = 0;
 
        while (n)
        {
            if (n->skipLen)
            {
                if (((masked ^ n->skipPattern) & n->skipMask) != 0)
                    return nullptr;
                pos += n->skipLen;
            }
 
            if (const PrefixEntry* e = n->findPrefix(masked, len))
                return e->ptr;
 
            if (pos >= W) break;
 
            const uint16_t shift = static_cast<uint16_t>(W - pos - S);
            const uint8_t  idx   = static_cast<uint8_t>((masked >> shift) & SMASK);
            pos += S;
            n = n->getChild(idx);
        }
        return nullptr;
    }
 
    /**
     * Insert or update a prefix.
     * @param pfx   N bytes, network order.
     * @param len   Prefix length in bits [0..W].
     * @param entry Caller-owned value.
     * @return false if len > W.
     */
    bool insert(const uint8_t* pfx, uint8_t len, T* entry)
    {
        if (len > W) return false;
        const AddrT masked = applyMask(toHostOrder(pfx), len);
 
        // Ensure root exists.
        if (!root_.load(std::memory_order_acquire))
        {
            Node* fresh    = new Node();
            Node* expected = nullptr;
            if (!root_.compare_exchange_strong(expected, fresh,
                    std::memory_order_release, std::memory_order_relaxed))
                delete fresh;
        }
 
        insertAt(root_.load(std::memory_order_relaxed), masked, len, entry, 0);
        return true;
    }
 
    /**
     * Remove a prefix.
     * @return false if not found.
     */
    bool erase(const uint8_t* pfx, uint8_t len)
    {
        if (len > W) return false;
        const AddrT masked = applyMask(toHostOrder(pfx), len);
 
        Node* r = root_.load(std::memory_order_acquire);
        if (!r) return false;
        return eraseAt(r, nullptr, 0, masked, len, 0);
    }
 
    /**
     * Delete all nodes. Not safe to call concurrently.
     */
    void clear() noexcept
    {
        Node* old = root_.exchange(nullptr, std::memory_order_acq_rel);
        destroyAll(old);
    }
 
    /**
     * Visit every prefix in unspecified order.
     * @tparam F  void(AddrT host_order_addr, uint8_t len, T* ptr)
     */
    template<typename F>
    void forEach(F&& fn) const noexcept
    {
        const Node* r = root_.load(std::memory_order_acquire);
        if (r) forEachNode(r, std::forward<F>(fn));
    }
 
    // ── Static helpers (public for testing) ──────────────────────────────────
 
    /** Convert N network-order bytes to a host-order AddrT integer.
     *
     *  "Host order" here means: bit 0 of the address (MSB of byte 0) sits
     *  in the highest bit of the returned integer, so that
     *      extractBits(addr, 0, S) == addr >> (W - S)
     *  holds unconditionally on any platform.  This is NOT the same as the
     *  platform's native endianness — it is always big-endian integer order.
     */
    static AddrT toHostOrder(const uint8_t* net) noexcept
    {
        // Assemble bytes most-significant-first into the integer.
        // byte[0] lands in bits [W-1..W-8], byte[1] in [W-9..W-16], etc.
        // No platform-endianness swap needed: the integer arithmetic is
        // endian-agnostic and NetworkSpan does the same assembly internally.
        AddrT v = 0;
        for (uint8_t i = 0; i < N; ++i)
            v = (v << 8) | static_cast<AddrT>(net[i]);
        return v;
    }
 
    /**
     * Zero host bits beyond len. Works for any unsigned integer width.
     */
    static AddrT applyMask(AddrT addr, uint8_t len) noexcept
    {
        if (len == 0)  return AddrT{0};
        if (len >= W)  return addr;
        // Shift a full-width mask right by (W - len) positions.
        // Special-case: shifting by W bits is UB for fixed-width types.
        AddrT mask = ~AddrT{0};
        mask <<= (W - len);
        return addr & mask;
    }
 
private:
 
    /**
     * Build skipMask: a mask with exactly `len` high bits set,
     * aligned to bit position `pos` within the W-bit address.
     * Example: W=32, pos=8, len=8 → 0x00FF0000
     */
    static AddrT makeMask(uint16_t pos, uint8_t len) noexcept
    {
        constexpr AddrT ALL_ONES = ~AddrT{0};
        if (len == 0) return AddrT{0};
        if (len >= W) return ALL_ONES;
        // len consecutive 1s in the low bits, shifted left to position pos.
        const AddrT low_mask = (AddrT{1} << len) - AddrT{1};
        return low_mask << (W - pos - len);
    }
 
    /**
     * Count how many bits of `a` and `b` agree, starting at `pos`,
     * for up to `maxBits` bits. Returns the count.
     */
    static uint8_t commonSkipLen(AddrT a, AddrT b,
                                  uint16_t pos, uint16_t maxBits) noexcept
    {
        uint8_t count = 0;
        for (uint16_t i = 0; i < maxBits; ++i)
        {
            const uint16_t bitPos = pos + i;
            const uint16_t shift  = static_cast<uint16_t>(W - bitPos - 1);
            if (((a >> shift) & 1) != ((b >> shift) & 1)) break;
            ++count;
        }
        return count;
    }
 
    /**
     * Push `entry` into `n->best` if it is non-null.
     */
    static void updateBest(Node* n, T* entry) noexcept
    {
        if (entry)
            n->best.store(entry, std::memory_order_release);
    }
 
    /**
     * Recompute `n->best` from the node's own prefix list.
     * Does NOT recurse into children — callers chain bottom-up.
     */
    static void recomputeBest(Node* n) noexcept
    {
        n->best.store(n->localBest(), std::memory_order_release);
    }
 
    /**
     * Recursive insertion into the subtree at `n`, having consumed `pos` bits.
     *
     * Invariant: on entry, the `pos` bits [0..pos) of `pfx` have been
     * validated or consumed by parent nodes and this node's skip.
     */
    void insertAt(Node* n, AddrT pfx, uint8_t len, T* entry, uint16_t pos)
    {
        // 1. Consume skip bits (Path Compression)
        if (n->skipLen > 0)
        {
            const uint8_t match = commonSkipLen(pfx, n->skipPattern, pos, n->skipLen);
            if (match < n->skipLen)
            {
                splitNode(n, pfx, len, entry, pos, match);
                return;
            }
            pos += n->skipLen;
        }
 
        // 2. Prefix terminates on this node.
        if (pos >= len || (len - pos) <= S)
        {
            n->insertPrefix(pfx, len, entry);
            // Update this node's best match to its own longest internal prefix.
            n->best.store(n->localBest(), std::memory_order_release);
            return;
        }
 
        // 3. Descend.
        const uint16_t shift = static_cast<uint16_t>(W - pos - S);
        const uint8_t  idx   = static_cast<uint8_t>((pfx >> shift) & SMASK);
        pos += S;
 
        Node* child = n->getChild(idx);
        if (!child)
        {
            // Create a new leaf
            Node* leaf   = new Node();
            leaf->depth  = static_cast<uint8_t>(pos / S);
 
            const uint16_t remaining = static_cast<uint16_t>(len - pos);
            if (remaining > 0)
            {
                leaf->skipLen     = static_cast<uint8_t>(remaining > 255 ? 255 : remaining);
                leaf->skipPattern = pfx;
                leaf->skipMask    = makeMask(pos, leaf->skipLen);
            }

            // INHERITANCE: The child must inherit the parent's current best 
            // as a fallback for LPM.
            T* parentBest = n->best.load(std::memory_order_relaxed);
            if (parentBest) leaf->best.store(parentBest, std::memory_order_relaxed);

            leaf->insertPrefix(pfx, len, entry);
            
            // Now update the child's best with its own new prefix.
            updateBest(leaf, entry);
            
            n->setChild(idx, leaf);
            
            return;
        }

        // INHERITANCE: Before descending, ensure the child knows about the 
        // parent's best match if the child doesn't have a better one.
        T* parentBest = n->best.load(std::memory_order_relaxed);
        if (parentBest) updateBest(child, parentBest);
 
        insertAt(child, pfx, len, entry, pos);
    }     

    void splitNode(Node* n, AddrT pfx, uint8_t len, T* entry,
                   uint16_t pos, uint8_t matchLen)
    {
        const uint8_t  oldSkipLen = n->skipLen;
        const AddrT    oldPattern = n->skipPattern;

        // Create a new sub-node that will hold n's original data
        Node* sub      = new Node();
        
        // Move prefixes from n to sub
        if (n->nPrefixes) {
            for (auto* p = n->prefixBegin(); p != n->prefixEnd(); ++p)
                sub->insertPrefix(p->addr, p->len, p->ptr);
            
            delete[] n->heapPfx;
            n->heapPfx = nullptr;
            n->nPrefixes = 0;
        }

        // Move children from n to sub
        if (n->children) {
            sub->children = n->children;
            n->children = nullptr;
        }

        // Setup sub-node skip bits (the remaining skip bits after the split)
        const uint16_t branchPos = pos + matchLen;
        const uint16_t subPos    = branchPos + S;
        
        if (oldSkipLen > matchLen + S) {
            sub->skipLen = oldSkipLen - (matchLen + S);
            sub->skipPattern = oldPattern;
            sub->skipMask = makeMask(subPos, sub->skipLen);
        }

        // Recompute sub's best now that it has its prefixes back
        sub->best.store(sub->localBest(), std::memory_order_relaxed);

        // Update original node n to only cover the matched skip prefix
        n->skipLen = matchLen;
        n->skipMask = makeMask(pos, matchLen);
        n->best.store(nullptr, std::memory_order_release); // Will be re-evaluated

        // Link sub as a child of n at the diverging bit
        const uint16_t shift = static_cast<uint16_t>(W - branchPos - S);
        const uint8_t oldIdx = static_cast<uint8_t>((oldPattern >> shift) & SMASK);
        n->setChild(oldIdx, sub);

        // Insert the new entry into the newly restructured tree
        insertAt(n, pfx, len, entry, pos);
    } 
 
    /**
     * Recursive erasure. Returns true on success.
     * Updates `best` bottom-up after removal.
     * Prunes childless, prefix-free nodes.
     */
    bool eraseAt(Node* n, Node* parent, uint8_t parentIdx,
                 AddrT pfx, uint8_t len, uint16_t pos)
    {
        if (n->skipLen)
        {
            if (((pfx ^ n->skipPattern) & n->skipMask) != 0)
                return false;
            pos += n->skipLen;
        }
 
        if (n->removePrefix(pfx, len))
        {
            recomputeBest(n);
            if (n->isEmpty() && parent)
            {
                parent->removeChild(parentIdx);
                retireNode(n);
            }
            return true;
        }
 
        if (pos >= W) return false;
 
        const uint16_t shift = static_cast<uint16_t>(W - pos - S);
        const uint8_t  idx   = static_cast<uint8_t>((pfx >> shift) & SMASK);
        pos += S;
 
        Node* child = n->getChild(idx);
        if (!child) return false;
 
        const bool removed = eraseAt(child, n, idx, pfx, len, pos);
        if (removed)
            recomputeBest(n);  // child may have been pruned or changed
        return removed;
    }
 
    template<typename F>
    static void forEachNode(const Node* n, F&& fn) noexcept
    {
        for (const PrefixEntry* p = n->prefixBegin(); p != n->prefixEnd(); ++p)
            fn(p->addr, p->len, p->ptr);
 
        if (!n->children) return;
        for (uint8_t i = 0; i < FANOUT; ++i)
        {
            const Node* child = n->children->slots[i].load(std::memory_order_relaxed);
            if (child) forEachNode(child, fn);
        }
    }
 
 
    /** Post-order recursive deletion.  NOT deferred through RCU — this is
     *  only called from `clear()` after all writers have finished. */
    static void destroyAll(Node* n) noexcept
    {
        if (!n) return;
        if (n->children)
        {
            for (uint8_t i = 0; i < FANOUT; ++i)
            {
                Node* child = n->children->slots[i].load(std::memory_order_relaxed);
                if (child) destroyAll(child);
            }
        }
        delete n;
    }
 
    static void destroyNode(Node* n) noexcept { delete n; }
 
    /**
     * Retire a single node: immediately if !useRCU, deferred otherwise.
     * Used during incremental erase, not during clear().
     */
    static void retireNode(Node* n) noexcept
    {
        if (!n) return;
        // Recursively retire children too before the node itself,
        // so readers that still hold a pointer to n see valid children.
        if constexpr (useRCU)
        {
            // Schedule child retirements first.
            if (n->children)
            {
                for (uint8_t i = 0; i < FANOUT; ++i)
                {
                    Node* c = n->children->slots[i].load(std::memory_order_relaxed);
                    if (c) retireNode(c);
                }
            }
            auto deleter = [](void* ctx) noexcept {
                destroyAll(static_cast<Node*>(ctx));
            };
            utils::RCU::retire(deleter, n);
        }
        else
        {
            destroyAll(n);
        }
    }
 
    // ── Data members ──────────────────────────────────────────────────────────
    std::atomic<Node*> root_{nullptr};
};

} // namespace types

#endif // LPC_TRIE_HPP
