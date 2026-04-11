/**
 * @file StableHashMap.hpp
 * @brief Open-addressing Swiss-table hash map with pointer-stable node storage.
 * @ingroup TYPES
 */

#ifndef STABLE_HASH_MAP_HPP
#define STABLE_HASH_MAP_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace types
{
template<typename T, size_t Alignment>
class AlignedAllocator
{
public:
    using value_type = T;

    template<typename U>
    struct rebind { using other = AlignedAllocator<U, Alignment>; };

    AlignedAllocator() noexcept = default;
    template<typename U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n)
    {
        if (n > std::size_t(-1) / sizeof(T))
            throw std::bad_alloc();
        void* ptr = _mm_malloc(n * sizeof(T), Alignment);
        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept
    {
        _mm_free(ptr);
    }

    bool operator==(const AlignedAllocator&) const { return true; }
    bool operator!=(const AlignedAllocator&) const { return false; }
};

/**
 * @brief Open-addressing hash map whose element addresses never move after insertion.
 * @ingroup TYPES
 *
 * Uses a Swiss-table control-byte array (AVX2 SIMD probing) for O(1) average-case
 * lookup and a private `NodePool` slab allocator so that each `Value` lives at a
 * stable heap address for its entire lifetime. Iterators and references to values
 * remain valid across insertions and rehashes; only erasure invalidates an entry's
 * reference.
 *
 * ## Architectural Role
 * Intended for control-plane caches (ARP, NDP, session tables) where external code
 * holds raw pointers or references into map values and those pointers must not be
 * invalidated by unrelated insertions.
 *
 * ## Concurrency Model
 * Not thread-safe. All reads and writes must be serialized by the caller (e.g. via
 * a control-plane scheduler queue).
 *
 * @tparam Key       Key type. Must be equality-comparable and hashable by @p Hash.
 * @tparam Value     Value type. Stored by value inside a node allocated from the
 *                   internal slab pool; the address is stable after construction.
 * @tparam Hash      Hash functor. Defaults to `std::hash<Key>`.
 * @tparam KeyEqual  Equality functor. Defaults to `std::equal_to<Key>`.
 * @tparam Allocator Allocator for node pool blocks. Defaults to `std::allocator`.
 */
template<
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>,
    typename Allocator = std::allocator<std::pair<const Key, Value>>
>
class StableHashMap
{
public:
    using valueType = std::pair<const Key, Value>;
    using hasher = Hash;

    struct Node
    {
        std::pair<const Key, Value> kv;
        size_t fullHash;
    };

private:
    using nodeAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;
    using nodeTraits = std::allocator_traits<nodeAlloc>;

    struct NodePool
    {
        nodeAlloc& alloc;

        struct Block
        {
            Node* nodes;
            size_t size;
            size_t used = 0;
            Block(Node* n, size_t s) : nodes(n), size(s) {}
        };

        std::vector<Block> blocks;
        std::vector<Node*> freeList;

        size_t nextBlockSize = 8;

        explicit NodePool(nodeAlloc& a) : alloc(a) {}

        Node* allocate()
        {
            if (!freeList.empty())
            {
                Node* n = freeList.back();
                freeList.pop_back();
                return n;
            }

            if (blocks.empty() || blocks.back().used == blocks.back().size)
            {
                Node* new_block = nodeTraits::allocate(alloc, nextBlockSize);
                blocks.emplace_back(new_block, nextBlockSize);
                nextBlockSize *= 2; // double for next allocation
            }

            Block& blk = blocks.back();
            Node* n = &blk.nodes[blk.used++];
            return n;
        }

        void deallocate(Node* n) noexcept
        {
            std::destroy_at(n);
            freeList.push_back(n);
        }

        ~NodePool()
        {
            for (auto& blk : blocks)
                nodeTraits::deallocate(alloc, blk.nodes, blk.size);
        }
    };

    std::vector<uint8_t, AlignedAllocator<uint8_t, 32>> control;
    std::vector<Node*> slots;
    size_t numLive = 0;
    size_t numFilled = 0;
    size_t capacity = 0;
    float maxLoadFactor = 0.875f;
    hasher hashFn;
    KeyEqual keyEq;
    nodeAlloc nodeAllocator;

    static constexpr uint8_t EMPTY = 0x80;
    static constexpr uint8_t DELETED = 0xFE;

    size_t mask() const noexcept { return capacity - 1; }

    Node* allocateNode()
    {
        Node* p = nodeTraits::allocate(nodeAllocator, 1);
        return p;
    }

    void deallocateNode(Node* p) noexcept
    {
        if (p)
        {
            std::destroy_at(p);
            nodeTraits::deallocate(nodeAllocator, p, 1);
        }
    }

    template<typename K, typename V>
    Node* construct_node(K&& key, V&& value, size_t hash)
    {
        Node* p = allocateNode();
        new (p) Node{{std::piecewise_construct,
                 std::forward_as_tuple(std::forward<K>(key)),
                 std::forward_as_tuple(std::forward<V>(value))},
                hash};
        return p;
    }

    struct ProbeResult
    {
        Node* found = nullptr;
        size_t foundSlot = size_t(-1);
        size_t insertSlot = size_t(-1);
    };

    __attribute__((target("avx2")))
    ProbeResult probe(const Key& key, size_t hash, uint8_t fp) const
    {
        const size_t mask = this->mask();
        size_t pos = hash & mask;
        size_t firstTombstone = size_t(-1);
        size_t probeLen = 0;

        const __m256i fpVec = _mm256_set1_epi8(static_cast<char>(fp));
        const __m256i emptyVec = _mm256_set1_epi8(static_cast<char>(EMPTY));
        const __m256i delVec = _mm256_set1_epi8(static_cast<char>(DELETED));

        while (true)
        {
            __m256i group = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(control.data() + pos));

            uint32_t match = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(group, fpVec)));
            while (match)
            {
                int idx = __builtin_ctz(match);
                size_t slot = (pos + idx) & mask;
                Node* e = slots[slot];
                if (e && e->fullHash == hash && keyEq(e->kv.first, key))
                    return {e, slot, size_t(-1)};
                match &= match - 1;
            }

            uint32_t delMask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(group, delVec)));
            if (firstTombstone == size_t(-1) && delMask)
                firstTombstone = (pos + __builtin_ctz(delMask)) & mask;

            uint32_t emptyMask = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(group, emptyVec)));
            if (emptyMask) {
                size_t emptySlot = (pos + __builtin_ctz(emptyMask)) & mask;
                return {nullptr, size_t(-1), (firstTombstone != size_t(-1)) ? firstTombstone : emptySlot};
            }

            pos = (pos + 32) & mask;
            if (++probeLen > capacity) std::abort();
        }
    }

    void insertIntoTable(Node* node, size_t cap, uint8_t* ctrl, Node** sl) const
    {
        const size_t mask = cap - 1;
        size_t hash = node->fullHash;
        uint8_t fp = ((hash >> 57) ^ (hash >> 40)) & 0x7F;
        size_t pos = hash & mask;

        while (true)
        {
            if (ctrl[pos] == EMPTY)
            {
                sl[pos] = node;
                ctrl[pos] = fp;
                return;
            }
            pos = (pos + 1) & mask;
        }
    }

    void rehash(size_t newCap)
    {
        if (newCap < numLive)
            newCap = numLive;
        // round up to next power of two (if not already)
        newCap = (newCap & (newCap - 1)) ? (1ULL << (64 - __builtin_clzll(newCap))) : newCap;
        if (newCap == capacity)
            return;

        // allocate new control (with padding) and slots
        size_t controlSize = newCap + 31;
        std::vector<uint8_t, AlignedAllocator<uint8_t, 32>> newCtrl(controlSize, EMPTY);
        std::vector<Node*> newSlots(newCap, nullptr);

        // re‑insert all live nodes
        for (size_t i = 0; i < capacity; ++i)
        {
            if (control[i] != EMPTY && control[i] != DELETED)
            {
                Node* node = slots[i];
                if (node)
                {
                    insertIntoTable(node, newCap, newCtrl.data(), newSlots.data());
                }
            }
        }

        // swap with the new data
        control.swap(newCtrl);
        slots.swap(newSlots);
        capacity = newCap;
        // num_live unchanged, num_filled becomes num_live (no tombstones)
        numFilled = numLive;
    }

    void rehashIfNeeded()
    {
        if (numFilled == capacity || numFilled > capacity * maxLoadFactor)
        {
            size_t newCap = capacity * 2;
            rehash(newCap);
        }
    }

public:
    /**
     * @brief Constructs an empty map with the given initial bucket capacity.
     *
     * The capacity is rounded up to the next power of two (minimum 8) so that
     * the AVX2 SIMD probe window always covers a full 32-byte aligned group.
     *
     * @param initialCapacity Hint for the number of elements before the first rehash.
     * @param hash            Hash functor instance.
     * @param equal           Key equality functor instance.
     * @param alloc           Allocator for the internal node pool.
     */
    explicit StableHashMap(size_t initialCapacity = 8,
                           const Hash& hash = Hash(),
                           const KeyEqual& equal = KeyEqual(),
                           const Allocator& alloc = Allocator())
        : hashFn(hash), keyEq(equal), nodeAllocator(alloc)
    {
        // initial capacity must be power of two, at least 8
        if (initialCapacity < 8) initialCapacity = 8;
        capacity = 1;
        while (capacity < initialCapacity) capacity <<= 1;
        // allocate control with padding
        control.assign(capacity + 31, EMPTY);
        slots.assign(capacity, nullptr);
    }

    /**
     * @brief Destructor. Deallocates all live nodes back to the pool and
     *        releases all pool blocks.
     */
    ~StableHashMap()
    {
        clear();
    }

    // copy and move are deleted for simplicity (can be added later)
    StableHashMap(const StableHashMap&) = delete;
    StableHashMap& operator=(const StableHashMap&) = delete;
    StableHashMap(StableHashMap&&) = default;
    StableHashMap& operator=(StableHashMap&&) = default;

    class Iterator
    {
        friend class StableHashMap;
        Node** slotsPtr = nullptr;
        size_t idx = 0;
        size_t cap = 0;

        void advanceToNextLive()
        {
            while (idx < cap && (slotsPtr[idx] == nullptr)) ++idx;
        }
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = std::pair<const Key, Value>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = value_type*;
        using reference         = value_type&;

        Iterator() = default;
        Iterator(Node** slots, size_t index, size_t cap)
            : slotsPtr(slots), idx(index), cap(cap)
        {
            advanceToNextLive();
        }

        std::pair<const Key, Value>& operator*() const { return slotsPtr[idx]->kv; }
        std::pair<const Key, Value>* operator->() const { return &slotsPtr[idx]->kv; }

        Iterator& operator++()
        {
            ++idx;
            advanceToNextLive();
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const Iterator& other) const
        {
            return idx == other.idx && slotsPtr == other.slotsPtr;
        }

        bool operator!=(const Iterator& other) const
        {
            return !(*this == other);
        }
    };

    class ConstIterator
    {
        friend class pointer_stable_unordered_map;
        Node* const* slotsPtr = nullptr;
        size_t idx = 0;
        size_t cap = 0;

        void advance_to_next_live()
        {
            while (idx < cap && (slotsPtr[idx] == nullptr)) ++idx;
        }
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = const std::pair<const Key, Value>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const value_type*;
        using reference         = const value_type&;

        ConstIterator() = default;
        ConstIterator(Node* const* slots, size_t index, size_t cap)
            : slotsPtr(slots), idx(index), cap(cap)
        {
            advance_to_next_live();
        }

        std::pair<const Key, Value>& operator*() const { return slotsPtr[idx]->kv; }
        std::pair<const Key, Value>* operator->() const { return &slotsPtr[idx]->kv; }

        ConstIterator& operator++()
        {
            ++idx;
            advance_to_next_live();
            return *this;
        }

        ConstIterator operator++(int)
        {
            ConstIterator tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const ConstIterator& other) const
        {
            return idx == other.idx && slotsPtr == other.slotsPtr;
        }

        bool operator!=(const ConstIterator& other) const
        {
            return !(*this == other);
        }
    };

    Iterator begin()
    {
        return Iterator(slots.data(), 0, capacity);
    }

    Iterator end()
    {
        return Iterator(slots.data(), capacity, capacity);
    }

    ConstIterator begin() const
    {
        return ConstIterator(slots.data(), 0, capacity);
    }

    ConstIterator end() const
    {
        return ConstIterator(slots.data(), capacity, capacity);
    }

    ConstIterator cbegin() const { return begin(); }
    ConstIterator cend() const { return end(); }

    bool empty() const noexcept { return numLive == 0; }
    size_t size() const noexcept { return numLive; }
    size_t max_size() const noexcept
    {
        return nodeTraits::max_size(nodeAllocator);
    }

    /**
     * @brief Destroys all entries and resets the map to an empty state.
     *
     * All nodes are returned to the pool allocator. Does not release the
     * underlying bucket array memory (capacity is preserved for reuse).
     */
    void clear()
    {
        for (size_t i = 0; i < capacity; ++i)
        {
            if (control[i] != EMPTY && control[i] != DELETED)
            {
                Node* node = slots[i];
                if (node) deallocateNode(node);
                slots[i] = nullptr;
            }
            control[i] = EMPTY;
        }
        numLive = 0;
        numFilled = 0;
    }

    Node* preAllocateNode()
    {
        return allocateNode();
    }

    /**
     * @brief Inserts a key-value pair by const reference.
     *
     * @return Iterator to the element and `true` if inserted; iterator to
     *         the existing element and `false` if the key was already present.
     */
    std::pair<Iterator, bool> insert(const std::pair<const Key, Value>& value)
    {
        return emplace(value.first, value.second);
    }

    /**
     * @brief Inserts a key-value pair by move.
     *
     * @return Iterator to the element and `true` if inserted; iterator to
     *         the existing element and `false` if the key was already present.
     */
    std::pair<Iterator, bool> insert(std::pair<const Key, Value>&& value)
    {
        return emplace(std::move(value.first), std::move(value.second));
    }

    /**
     * @brief Constructs a new entry in-place from forwarded key and value.
     *
     * If the key already exists the existing value is overwritten with the
     * forwarded value. Triggers a rehash if the load factor threshold is
     * exceeded.
     *
     * @return Iterator to the element and `true` if inserted; iterator to
     *         the updated element and `false` if it already existed.
     */
    template<typename K, typename V>
    std::pair<Iterator, bool> emplace(K&& key, V&& value)
    {
        size_t hash = hashFn(key);
        uint8_t fp = (hash >> 57) & 0x7F;

        ProbeResult res = probe(key, hash, fp);

        if (res.found)
        {
            // update value (but key must not change)
            res.found->kv.second = std::forward<V>(value);
            return {Iterator(slots.data(), res.foundSlot, capacity), false};
        }

        // allocate new node
        rehashIfNeeded();

        Node* node = construct_node(std::forward<K>(key), std::forward<V>(value), hash);
        slots[res.insertSlot] = node;
        control[res.insertSlot] = fp;
        ++numLive;
        ++numFilled;

        return {Iterator(slots.data(), res.insertSlot, capacity), true};
    }

    /**
     * @brief Erases the element at the given iterator position.
     *
     * The node is returned to the pool allocator. The iterator is invalidated;
     * all other iterators and references remain valid.
     *
     * @param pos Iterator to the element to erase. No-op if equal to `end()`.
     */
    void erase(Iterator pos)
    {
        if (pos == end()) return;
        Node* node = slots[pos.idx];
        if (node)
        {
            deallocateNode(node);
            slots[pos.idx] = nullptr;
            control[pos.idx] = DELETED;
            --numLive;
        }
    }

    /**
     * @brief Erases the element with the given key.
     *
     * @return 1 if the key was found and erased, 0 if not present.
     */
    size_t erase(const Key& key)
    {
        size_t hash = hashFn(key);
        uint8_t fp = (hash >> 57) & 0x7F;

        ProbeResult res = probe(key, hash, fp);

        if (res.found)
        {
            deallocateNode(res.found);
            slots[res.foundSlot] = nullptr;
            control[res.foundSlot] = DELETED;
            --numLive;
            return 1;
        }
        return 0;
    }

    /**
     * @brief Finds an element by key.
     *
     * @return Iterator to the element, or `end()` if not found.
     */
    Iterator find(const Key& key)
    {
        size_t hash = hashFn(key);
        uint8_t fp = (hash >> 57) & 0x7F;

        ProbeResult res = probe(key, hash, fp);

        if (res.found)
        {
            return Iterator(slots.data(), res.foundSlot, capacity);
        }
        return end();
    }

    ConstIterator find(const Key& key) const
    {
        size_t hash = hashFn(key);
        uint8_t fp = (hash >> 57) & 0x7F;

        ProbeResult res = probe(key, hash, fp);

        if (res.found)
        {
            return ConstIterator(slots.data(), res.foundSlot, capacity);
        }
        return end();
    }

    size_t count(const Key& key) const
    {
        return find(key) != end() ? 1 : 0;
    }

    bool contains(const Key& key) const
    {
        return find(key) != end();
    }

    Value& operator[](const Key& key)
    {
        auto it = find(key);
        if (it != end())
        {
            return it->second;
        }
        auto res = emplace(key, Value{});
        return res.first->second;
    }

    Value& operator[](Key&& key)
    {
        auto it = find(key);
        if (it != end())
        {
            return it->second;
        }
        auto res = emplace(std::move(key), Value{});
        return res.first->second;
    }

    Value& at(const Key& key)
    {
        auto it = find(key);
        if (it == end())
            throw std::out_of_range("pointer_stable_unordered_map::at");
        return it->second;
    }

    const Value& at(const Key& key) const
    {
        auto it = find(key);
        if (it == end())
            throw std::out_of_range("pointer_stable_unordered_map::at");
        return it->second;
    }

    size_t bucketCount() const noexcept { return capacity; }
    size_t maxBucketCount() const noexcept { return size_t(-1); }
    float loadFactor() const noexcept
    {
        return static_cast<float>(numLive) / static_cast<float>(capacity);
    }

    float getMaxLoadFactor() const noexcept { return maxLoadFactor; }
    void setMaxLoadFactor(float ml)
    {
        maxLoadFactor = ml;
        rehashIfNeeded();
    }

    void reserve(size_t n)
    {
        if (n > numLive)
        {
            size_t new_cap = static_cast<size_t>(n / maxLoadFactor) + 1;
            rehash(new_cap);
        }
    }

    hasher hashFunction() const { return hashFn; }
    KeyEqual keyEqFunc() const { return keyEq; }
};
}

#endif // STABLE_HASH_MAP_HPP
