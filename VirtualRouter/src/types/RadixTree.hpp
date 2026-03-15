// RadixTree.hpp

#ifndef RADIX_TREE_HPP
#define RADIX_TREE_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <RCU.hpp>

/**
 * @class RadixTree<N, T>
 *
 * Lock-free (single-writer) binary Patricia tree keyed on fixed-width
 * byte-array prefixes in network (big-endian) bit order.
 * 
 * Template Parameter
 * N    - address width in bytes (4 = ipv4, 16 = ipv6)
 * T    - value type directly in each node
 * 
 * Each internal node tests exactly one bit (node::bit) to choose left/right.
 * a memcmp is preformed ONLY to confirm a canidate node actually covers the
 * query - never as part of the decent decision.
 *
 * RetireFn - Passed to erase() / clear() so callers control reclamation:
 */
template<uint8_t N, typename T, bool useRCU = false>
class RadixTree
{
    static_assert(N > 0, "address width must be at least 1 byte");

public:
    static constexpr uint16_t W = static_cast<uint16_t>(N) * 8;

    struct Node
    {
        uint8_t prefix[N];
        uint16_t bit;        // discriminator bit index; W = leaf/no-test
        uint8_t length;     // prefix length in bits [0, W]
        std::atomic<Node*> left{nullptr};
        std::atomic<Node*> right{nullptr};
        T                  value;

        Node(const uint8_t* pfx, uint8_t len, uint16_t b, T&& val)
            : bit(b), length(len), value(std::forward<T>(val))
        {
            std::memcpy(prefix, pfx, N);
        }

        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;
    };

    template <typename F>
    void forEach(F&& fn) const noexcept
    {
        forEachNode(root.load(std::memory_order_acquire), std::forward<F>(fn));
    }

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

    void clear()
    {
        Node* old = root.exchange(nullptr, std::memory_order_acq_rel);
        destroyAll(old);
    }

    static bool bitAt(const uint8_t* addr, uint16_t i) noexcept
    {
        if (i >= W) return false;
        return (addr[i >> 3] >> (7 - (i & 7))) & 1;
    }

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

    enum class EraseResult : uint8_t { KEEP, REMOVE, NOT_FOUND };

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

    void destroyAll(Node* n)
    {
        if (!n) return;
        destroyAll(n->left.load(std::memory_order_relaxed));
        destroyAll(n->right.load(std::memory_order_relaxed));
        retire(n);
    }

    static void destroy(Node* n) { delete n; }

    static void retire(Node* n)
    {
        if (!n) return;
        if constexpr (useRCU)
            RCU::retire([n]{ destroy(n); });
        else
            destroy(n);
    }

    std::atomic<Node*> root{nullptr};
};

#endif // RADIX_TREE_HPP
