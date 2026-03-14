// LPCTrie.hpp
#pragma once
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <RCU.hpp>

template<uint8_t N, typename T, uint8_t S = 8, bool useRCU = false>
class LPCTrie
{
    static_assert(N > 0,        "N must be >= 1");
    static_assert(S >= 1 && S <= 8, "stride S must be 1..8 bits");

public:
    static constexpr uint16_t W       = static_cast<uint16_t>(N) * 8;
    static constexpr uint8_t  FANOUT  = static_cast<uint8_t>(1u << S);
    static constexpr uint8_t  SMASK   = static_cast<uint8_t>(FANOUT - 1);

    static constexpr uint8_t  SPARSE_THRESHOLD = 6;

    struct PrefixEntry
    {
        uint8_t prefix[N];
        uint8_t len;        // prefix length in bits
        T*      ptr;        // caller-owned value pointer

        bool operator<(const PrefixEntry& o) const noexcept
        {
            if (len != o.len) return len < o.len;
            return std::memcmp(prefix, o.prefix, N) < 0;
        }
    };

    struct Node;

    struct ChildSlot
    {
        uint8_t  index;     // stride-bit index [0, FANOUT)
        uint8_t  _pad[7];
        Node*    child;
    };

    struct Node
    {
        uint8_t  skipBits[N];
        uint8_t  skipLen;
        uint8_t  depth;

        std::atomic<T*> best{nullptr};

        static constexpr uint8_t PREFIX_INLINE = 4;
        uint8_t        nPrefixes{0};
        PrefixEntry*   prefixes{nullptr};
        PrefixEntry    inlinePfx[PREFIX_INLINE];

        bool     dense{false};
        uint8_t  nChildren{0};

        union ChildStore
        {
            struct Sparse
            {
                ChildSlot slots[SPARSE_THRESHOLD];
            } sparse;

            struct Dense
            {
                uint64_t  bitmap;
                Node**    children;     // packed array, heap-allocated
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
        PrefixEntry* prefixEnd()   noexcept { return prefixBegin() + nPrefixes; }
        const PrefixEntry* prefixEnd() const noexcept { return prefixBegin() + nPrefixes; }

        PrefixEntry* findPrefix(const uint8_t* pfx, uint8_t len) noexcept
        {
            PrefixEntry* b = prefixBegin();
            PrefixEntry* e = prefixEnd();
            for (PrefixEntry* p = b; p != e; ++p)
                if (p->len == len && std::memcmp(p->prefix, pfx, N) == 0)
                    return p;
            return nullptr;
        }

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

        T* localBest() const noexcept
        {
            if (!nPrefixes) return nullptr;
            return prefixEnd()[-1].ptr;
        }

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

    T* lookup(const uint8_t* addr) const noexcept
    {
        Node* n    = root_.load(std::memory_order_acquire);
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

    T* lookupExact(const uint8_t* pfx, uint8_t len) const noexcept
    {
        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* n   = root_.load(std::memory_order_acquire);
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

    bool insert(const uint8_t* pfx, uint8_t len, T* entry)
    {
        if (len > W) return false;

        uint8_t masked[N];
        applyMask(pfx, len, masked);

        // Ensure root
        if (!root_.load(std::memory_order_acquire))
        {
            Node* fresh = new Node();
            Node* expected = nullptr;
            if (!root_.compare_exchange_strong(expected, fresh,
                    std::memory_order_release, std::memory_order_acquire))
                delete fresh;
        }

        insertAt(root_.load(std::memory_order_acquire), masked, len, entry, 0);
        return true;
    }

    bool erase(const uint8_t* pfx, uint8_t len)
    {
        if (len > W) return false;

        uint8_t masked[N];
        applyMask(pfx, len, masked);

        Node* r = root_.load(std::memory_order_acquire);
        if (!r) return false;

        return eraseAt(r, nullptr, 0, masked, len, 0);
    }

    void clear()
    {
        Node* old = root_.exchange(nullptr, std::memory_order_acq_rel);
        destroyAll(old);
    }

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

    // Write pfx masked to len bits into out.
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

    static void updateBest(Node* n, T* candidate) noexcept
    {
        if (candidate)
            n->best.store(candidate, std::memory_order_release);
    }

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

    static void retireNode(Node* n)
    {
        if (!n) return;
        if constexpr (useRCU)
            RCU::retire([n]{ destroyNode(n); });
        else
            destroyNode(n);
    }

    std::atomic<Node*> root_{nullptr};
};
