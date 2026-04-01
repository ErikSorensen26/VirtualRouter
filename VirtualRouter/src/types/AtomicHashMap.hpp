/**
 * @file AtomicHashMap.hpp
 * @brief Lock-free chained hash map with RCU-deferred node reclamation.
 */

#ifndef ATOMIC_HASH_MAP_HPP
#define ATOMIC_HASH_MAP_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <xmmintrin.h>
#include <RCU.hpp>
#include <cstring>
#include <ByteUtils.hpp>

namespace types
{
/**
 * @brief Lock-free hash map supporting concurrent reads and serialized writes,
 *        backed by RCU-safe node reclamation.
 * @ingroup TYPES
 *
 * Each bucket is the head of an intrusive singly-linked list of @c Node objects.
 * Bucket heads are stored as @c std::atomic<Node*> so that readers can traverse
 * a bucket chain without holding a lock, observing a consistent snapshot at any
 * point in time.
 *
 * Node memory is managed by a private block allocator to amortize allocation
 * overhead on insertion. Erased nodes are handed to @ref utils::RCU::retire so
 * that readers currently traversing the chain finish before the node's memory
 * is reclaimed.
 *
 * ## Architectural Role
 * Used for tables that are read on every packet (e.g. neighbor or session
 * lookup) and written infrequently (session up/down events). The bucket count
 * is fixed at construction; no rehashing occurs.
 *
 * ## Lifecycle & Ownership
 * The map is constructed with a fixed number of buckets. All nodes are owned
 * by the internal block allocator; values are copied by the @c Node constructor.
 * Destruction does not run RCU retirement — all memory owned by the allocator
 * is freed directly when the map is destroyed, so the map must not be destroyed
 * while readers are active unless the caller coordinates externally.
 *
 * ## Concurrency Model
 * - Multiple concurrent readers are safe via RCU read-side guards.
 * - Only one writer thread at a time. Concurrent inserts or erases on the same
 *   bucket race on the CAS loop and will eventually succeed, but concurrent
 *   modifications to the same key from two writers are not safe.
 *
 * @warning The block allocator is not thread-safe. @c insert must be called
 *          from a single writer thread.
 *
 * @tparam Key    Key type. Must be equality-comparable and hashable by @p Hasher.
 * @tparam Value  Value type. Must be copy-constructible; stored by value in each node.
 * @tparam Hasher Hash functor type. Must satisfy the standard @c Hash concept:
 *                callable as @c size_t(const Key&).
 */
template<
    typename Key,
    typename Value,
    typename Hash     = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>,
    int      LoadPct  = 75   // resize when (size+deleted)/capacity >= LoadPct%
>
class AtomicHashMap
{
    static_assert(std::is_trivially_copyable_v<Key>,
                  "Key must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<Value>,
                  "Value must be trivially copyable");
    static_assert(LoadPct > 0 && LoadPct < 100,
                  "LoadPct must be in (0, 100)");

    static constexpr uint8_t kEmpty   = 0x80;
    static constexpr uint8_t kDeleted = 0xFE;

    static constexpr bool ctrlOccupied(uint8_t c) noexcept
    {
        return (c & 0x80) == 0;
    }

    static constexpr uint8_t makeH2(size_t hash) noexcept
    {
        return static_cast<uint8_t>(hash & 0x7F);
    }

    struct Slot
    {
        Key   key;
        Value value;
    };

    struct TableEntry
    {
        Slot slot;
        uint8_t crtl;
    };

    struct Table
    {
        size_t   cap;      // slot count, always a power of two
        size_t   mask;     // cap - 1, for fast modulo
        size_t   size;     // live (occupied) slot count
        size_t   deleted;  // tombstone slot count
        uint8_t* ctrl;     // [cap] control bytes
        Slot*    slots;    // [cap] key-value pairs

        static Table* make(size_t capacity)
        {
            capacity = ceilPow2(capacity);
            Table* t   = new Table{};
            t->cap     = capacity;
            t->mask    = capacity - 1;
            t->size    = 0;
            t->deleted = 0;
            t->ctrl    = static_cast<uint8_t*>(std::malloc(capacity));
            t->slots   = static_cast<Slot*>(
                             std::aligned_alloc(alignof(Slot),
                                                capacity * sizeof(Slot)));
            if (!t->ctrl || !t->slots) throw std::bad_alloc();
            std::memset(t->ctrl, kEmpty, capacity);
            return t;
        }

        static Table* clone(const Table* src)
        {
            Table* t   = new Table{};
            t->cap     = src->cap;
            t->mask    = src->mask;
            t->size    = src->size;
            t->deleted = src->deleted;
            t->ctrl    = static_cast<uint8_t*>(std::malloc(src->cap));
            t->slots   = static_cast<Slot*>(
                             std::aligned_alloc(alignof(Slot),
                                                src->cap * sizeof(Slot)));
            if (!t->ctrl || !t->slots) throw std::bad_alloc();
            std::memcpy(t->ctrl,  src->ctrl,  src->cap);
            std::memcpy(t->slots, src->slots, src->cap * sizeof(Slot));
            return t;
        }

        static void destroy(Table* t)
        {
            if (!t) return;
            std::free(t->ctrl);
            std::free(t->slots);
            delete t;
        }

        const Value* find(const Key& key, size_t hsh, const KeyEqual& eq) const noexcept
        {
            const uint8_t h = makeH2(hsh);
            size_t idx = hsh & mask;

            for (size_t i = 0; i < cap; ++i)
            {
                uint8_t c = ctrl[idx];

                if (c == kEmpty)
                    return nullptr;          // early exit

                if (c == h && eq(slots[idx].key, key))
                    return &slots[idx].value;

                // use a bigger step if desired
                idx = (idx + 4) & mask;      // SwissMap-style
            }
            return nullptr;
        }

        void insert(const Key& key, const Value& value, size_t hsh) noexcept
        {
            const uint8_t h = makeH2(hsh);
            size_t idx = hsh & mask;

            for (size_t i = 0; i < cap; ++i)
            {
                uint8_t& c = ctrl[idx];
                if (c == kEmpty || c == kDeleted)
                {
                    if (c == kDeleted) --deleted;
                    slots[idx] = {key, value};
                    c = h;
                    ++size;
                    return;
                }
                idx = (idx + 1) & mask;
            }
        }

        void update(const Key& key, const Value& value,
                    size_t hsh, const KeyEqual& eq) noexcept
        {
            const uint8_t h = makeH2(hsh);
            size_t idx = hsh & mask;

            for (size_t i = 0; i < cap; ++i)
            {
                uint8_t c = ctrl[idx];
                if (c == h && eq(slots[idx].key, key))
                {
                    slots[idx].value = value;
                    return;
                }
                if (c == kEmpty) return;  // key absent (caller contract violated)
                idx = (idx + 1) & mask;
            }
        }

        void erase(const Key& key, size_t hsh,
                   const KeyEqual& eq) noexcept
        {
            const uint8_t h = makeH2(hsh);
            size_t idx = hsh & mask;

            for (size_t i = 0; i < cap; ++i)
            {
                uint8_t c = ctrl[idx];
                if (c == h && eq(slots[idx].key, key))
                {
                    ctrl[idx] = kDeleted;
                    --size;
                    ++deleted;
                    return;
                }
                if (c == kEmpty) return;  // key absent (caller contract violated)
                idx = (idx + 1) & mask;
            }
        }

        bool needsResize() const noexcept
        {
            return (size + deleted) * 100 >= cap * LoadPct;
        }

        bool slotOccupied(size_t i) const noexcept
        {
            return ctrlOccupied(ctrl[i]);
        }

    private:
        static size_t ceilPow2(size_t n) noexcept
        {
            if (n <= 1) return 1;
            --n;
            n |= n >>  1; n |= n >>  2; n |= n >>  4;
            n |= n >>  8; n |= n >> 16; n |= n >> 32;
            return n + 1;
        }
    };

public:
    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = std::pair<const Key, const Value>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const value_type*;
        using reference         = const value_type&;

        const_iterator() noexcept : tbl(nullptr), idx(0) {}

        reference operator*() const noexcept
        {
            // Slot is {Key, Value}; pair<const Key, const Value> has the same
            // layout for trivially copyable types.
            return *reinterpret_cast<const value_type*>(&tbl->slots[idx]);
        }

        pointer operator->() const noexcept { return &**this; }

        const_iterator& operator++() noexcept { advance(); return *this; }
        const_iterator  operator++(int) noexcept
        {
            auto tmp = *this; advance(); return tmp;
        }

        friend bool operator==(const const_iterator& a,
                               const const_iterator& b) noexcept
        {
            return a.tbl == b.tbl && a.idx == b.idx;
        }
        friend bool operator!=(const const_iterator& a,
                               const const_iterator& b) noexcept
        {
            return !(a == b);
        }

    private:
        friend class AtomicHashMap;
        const_iterator(const Table* t, size_t i) noexcept : tbl(t), idx(i)
        {
            skip_empty();
        }

        void advance() noexcept { ++idx; skip_empty(); }

        void skip_empty() noexcept
        {
            while (tbl && idx < tbl->cap && !tbl->slotOccupied(idx))
                ++idx;
            if (tbl && idx >= tbl->cap) { tbl = nullptr; idx = 0; }
        }

        const Table* tbl;
        size_t       idx;
    };

    class Snapshot {
    public:
        Snapshot() noexcept : tbl(nullptr) {}

        Snapshot(Snapshot&& o) noexcept
            : guard(std::move(o.guard)), tbl(o.tbl) { o.tbl = nullptr; }

        Snapshot& operator=(Snapshot&& o) noexcept
        {
            if (this != &o) {
                guard = std::move(o.guard);
                tbl   = o.tbl;
                o.tbl = nullptr;
            }
            return *this;
        }

        const_iterator begin() const noexcept { return {tbl, 0}; }
        const_iterator end()   const noexcept { return {}; }
        size_t size()  const noexcept { return tbl ? tbl->size : 0; }
        bool   empty() const noexcept { return size() == 0; }

    private:
        friend class AtomicHashMap;
        Snapshot(utils::RCU::Guard&& g, const Table* t) noexcept
            : guard(std::move(g)), tbl(t) {}

        utils::RCU::Guard   guard;
        const Table* tbl;
    };

    explicit AtomicHashMap(size_t initial_capacity = 16)
        : current(Table::make(initial_capacity))
    {}

    ~AtomicHashMap()
    {
        // No readers at destruction; destroy directly without RCU.
        Table::destroy(current.load(std::memory_order_relaxed));
    }

    AtomicHashMap(const AtomicHashMap&)            = delete;
    AtomicHashMap& operator=(const AtomicHashMap&) = delete;

    /**
     * @brief Looks up a key and copies its value into @p out.
     *
     * Acquires an RCU read-side guard internally so the returned value is always
     * from a consistent snapshot. Safe to call from any thread concurrently.
     *
     * @param[out] out Receives the value if the key is found.
     * @return True if found and @p out was populated; false if the key is absent.
     */
    bool find(const Key& key, Value& out) const noexcept
    {
        utils::RCU::Guard guard;
        const Table* t = current.load(std::memory_order_acquire);
        const Value* v = t->find(key, hash(key), equal);
        if (v) { out = *v; return true; }
        return false;
    }

    /**
     * @brief Looks up a key and writes the low @p W bits of its value into @p out.
     *
     * Performs a width-dispatch write using `utils::writeU8/16/24/32/48/64/128`
     * so the result lands correctly regardless of host endianness. Acquires an
     * RCU read-side guard for the duration of the lookup.
     *
     * @tparam W Bit width to write. Must be one of 8, 16, 24, 32, 48, 64, 128
     *           and must not exceed `sizeof(Value) * 8`.
     * @param[out] out Destination buffer; must be at least `W/8` bytes.
     * @return True if found and @p out was written; false if the key is absent.
     */
    template<size_t W>
    bool findAndWrite(const Key& key, uint8_t* out) const noexcept
    requires ((W == 8 || W == 16 || W == 24 || W == 32 || W == 48 || W == 64 || W == 128) && (sizeof(Value) * 8) >= W)
    {
        utils::RCU::Guard guard;
        const Table* t = current.load(std::memory_order_acquire);
        const Value* v = t->find(key, hash(key), equal);
        if (v)
        {
            if constexpr (W == 8)
                out[0] = static_cast<uint8_t>(*v);
            else if constexpr (W == 16)
                utils::writeU16(out, static_cast<uint16_t>(*v));
            else if constexpr (W == 24)
                utils::writeU24(out, static_cast<uint32_t>(*v));
            else if constexpr (W == 32)
                utils::writeU32(out, static_cast<uint32_t>(*v));
            else if constexpr (W == 48)
                utils::writeU48(out, static_cast<uint64_t>(*v));
            else if constexpr (W == 64)
                utils::writeU64(out, static_cast<uint64_t>(*v));
            else
                utils::writeU128(out, static_cast<__uint128_t>(*v));
            return true;
        }
        return false;
    }

    /**
     * @brief Returns an RCU-guarded snapshot of the current table for safe iteration.
     *
     * The returned @ref Snapshot holds an RCU read-side guard for its lifetime.
     * Dropping the Snapshot releases the guard. Any write that occurs after the
     * snapshot is taken will not be visible through it.
     *
     * @note Do not hold a Snapshot across a call to @c insert or @c erase —
     *       the writer waits for all active guards to expire before freeing the
     *       old table, so holding a snapshot while writing will deadlock.
     */
    Snapshot snapshot() const noexcept
    {
        utils::RCU::Guard guard;
        const Table* t = current.load(std::memory_order_acquire);
        return Snapshot(std::move(guard), t);
    }

    /** Size at the moment of the call (consistent within one RCU guard). */
    size_t size() const noexcept
    {
        utils::RCU::Guard guard;
        return current.load(std::memory_order_acquire)->size;
    }

    bool empty() const noexcept { return size() == 0; }

    /**
     * @brief Inserts a new key-value pair or updates the value of an existing key.
     *
     * Clones the current table before mutating so concurrent readers always see
     * a consistent snapshot. Triggers a resize when the load factor exceeds the
     * configured @p LoadPct threshold.
     *
     * @return True if the key was newly inserted; false if the value was updated.
     *
     * @warning Must be called from a single writer thread. Concurrent inserts
     *          race on the internal block allocator and corrupt state.
     */
    bool insert(const Key& key, const Value& value)
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        Table* old = current.load(std::memory_order_relaxed);
        const size_t h = hash(key);

        if (old->find(key, h, equal))
        {
            // Key exists: clone, update, publish.
            Table* neo = Table::clone(old);
            neo->update(key, value, h, equal);
            publish(neo, old);
            return false;
        }

        // Key absent: clone (growing if needed), insert, publish.
        Table* neo = old->needsResize() ? grow(old) : Table::clone(old);
        neo->insert(key, value, h);
        publish(neo, old);
        return true;
    }

    /**
     * @brief Removes the entry with the given key.
     *
     * Clones the current table, marks the slot as deleted in the clone, and
     * publishes it. The old table is retired via RCU and freed once all active
     * readers have exited their critical sections.
     *
     * @return True if the key was found and removed; false if not present.
     */
    bool erase(const Key& key)
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        Table* old = current.load(std::memory_order_relaxed);
        const size_t h = hash(key);

        if (!old->find(key, h, equal))
            return false;

        Table* neo = Table::clone(old);
        neo->erase(key, h, equal);
        publish(neo, old);
        return true;
    }

    /**
     * @brief Removes all entries and resets to minimum capacity.
     *
     * Publishes a fresh empty table via RCU. The old table is retired and freed
     * once all active readers have exited.
     */
    void clear()
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        Table* old = current.load(std::memory_order_relaxed);
        publish(Table::make(16), old);
    }

    /**
     * @brief Pre-allocates table capacity for at least @p n entries.
     *
     * If the current capacity already accommodates @p n entries without
     * exceeding the load threshold, this is a no-op. Otherwise a new larger
     * table is published via RCU.
     *
     * @param n Minimum number of entries the table should accommodate.
     */
    void reserve(size_t n)
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        Table* old = current.load(std::memory_order_relaxed);

        // Cap needed so that n entries stay below LoadPct.
        const size_t needed_cap = (n * 100 + LoadPct - 1) / LoadPct;
        if (needed_cap <= old->cap) return;

        Table* neo = Table::make(needed_cap);
        rehash_into(old, neo);
        publish(neo, old);
    }

    /**
     * Rebuild at same capacity to eliminate tombstones.
     * No-op if there are no tombstones.
     */
    void rehash()
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        Table* old = current.load(std::memory_order_relaxed);
        if (old->deleted == 0) return;

        Table* neo = Table::make(old->cap);
        rehash_into(old, neo);
        publish(neo, old);
    }

private:
    /** Build a 2× table and populate it from src. */
    Table* grow(const Table* src) const
    {
        Table* dst = Table::make(src->cap * 2);
        rehash_into(src, dst);
        return dst;
    }

    /**
     * Copy all live entries from src into dst.
     * Uses copy, not move, so src stays valid for concurrent RCU readers.
     */
    void rehash_into(const Table* src, Table* dst) const
    {
        for (size_t i = 0; i < src->cap; ++i)
        {
            if (src->slotOccupied(i))
            {
                const Slot& s = src->slots[i];
                dst->insert(s.key, s.value, hash(s.key));
            }
        }
    }

    /**
     * Atomically publish neo, retire old through RCU.
     * Must be called under write_mutex_.
     */
    void publish(Table* neo, Table* old) noexcept
    {
        auto deleter = [](void* p) {
            Table::destroy(static_cast<Table*>(p));
        };
        current.store(neo, std::memory_order_release);
        utils::RCU::retire(deleter, old);
    }

    std::atomic<Table*> current;
    std::mutex          writeMutex;
    Hash                hash;
    KeyEqual            equal;
};
}

#endif // ATOMIC_HASH_MAP_HPP
