/**
 * @file TxBufferPool.h
 * @brief Lock-free slab allocator that supplies fixed-size blocks to TxBuffer instances.
 */

#ifndef TCP_TX_BUFFER_POOL_H
#define TCP_TX_BUFFER_POOL_H

#include <atomic>
#include <vector>

namespace transport::tcp
{
class TxBuffer;
struct PoolConfig;

/**
 * @brief Shared pool of fixed-size memory blocks used by all @ref TxBuffer instances.
 * @ingroup TCP_TX
 *
 * `TxBufferPool` allocates memory in large contiguous slabs and hands out
 * individual fixed-size `Block` objects through a lock-free LIFO free-list.
 * Each active @ref TxBuffer holds a chain of these blocks; when the buffer is
 * consumed or reset, blocks are returned to the free-list atomically.
 *
 * The pool grows on demand (up to `PoolConfig::maxBlocks` if non-zero) by
 * allocating a new slab. Slab ownership is held in a `std::vector` protected
 * by a mutex; slab growth is therefore not lock-free, but it is a rare slow
 * path. The hot path — `pop()` / `release()` — uses only atomic operations.
 *
 * ## Architectural Role
 * One pool is shared across all connections inside a single @ref TcpEngine.
 * This amortises allocation overhead and avoids per-connection heap fragmentation
 * for the transmit path. The pool is sized at engine construction time via
 * @ref PoolConfig.
 *
 * ## Lifecycle & Ownership
 * Created and destroyed exclusively by @ref TcpEngine. The destructor frees
 * all slab allocations. Any @ref TxBuffer still holding blocks at destructor
 * time will have dangling pointers — all `TxBuffer` objects must be reset
 * before the pool is destroyed.
 *
 * ## Concurrency Model
 * `pop()`, `addRef()`, and `release()` are safe to call concurrently from
 * multiple threads — they use lock-free atomic compare-exchange on the free-list
 * head. `grow()` acquires `growMtx` and is the only serialised operation.
 *
 * @warning Destroying the pool while any `TxBuffer` still holds blocks is
 * undefined behavior. All connections must be torn down before `TcpEngine`
 * destroys its pool.
 *
 * @see TxBuffer
 * @see PoolConfig
 */
class TxBufferPool final
{
public:
    /**
     * @brief Constructs the pool and pre-allocates the initial slab.
     * @ingroup TCP_TX
     *
     * Seeds the free-list with `PoolConfig::slabBlocks` blocks of size
     * `PoolConfig::blockSize`. The slab is owned by this pool until destruction.
     *
     * @param c Pool configuration (block size, initial slab depth, max blocks).
     */
    explicit TxBufferPool(PoolConfig& c) noexcept;

    /**
     * @brief Destroys the pool and frees all slab allocations.
     *
     * All slab memory is released via `operator delete`. Any @ref TxBuffer
     * that still holds blocks from this pool must be reset before this destructor runs.
     */
    ~TxBufferPool();

    TxBufferPool(const TxBufferPool&) = delete;
    TxBufferPool& operator=(const TxBufferPool&) = delete;

    /**
     * @brief Acquires a new, empty @ref TxBuffer backed by this pool.
     *
     * The returned buffer holds no blocks initially; blocks are allocated
     * on-demand when the first @ref TxBuffer::reserveSpan call is made.
     *
     * @return A new `TxBuffer` associated with this pool.
     */
    TxBuffer acquire() noexcept;

    size_t blockSize() const noexcept;

private:
    /**
     * @brief Intrusive singly-linked block node stored inline before each data region.
     *
     * The data region immediately follows the `Block` header in memory:
     * `reinterpret_cast<uint8_t*>(this + 1)`. `rd` and `wr` are byte offsets
     * into that data region marking the current read and write cursors.
     */
    struct Block final
    {
        std::atomic<uint32_t> refs; ///< Reference count; block is returned to the free-list when it reaches 0.
        Block* next = nullptr;      ///< Next block in a chain (TxBuffer linked list) or free-list.
        uint32_t rd = 0;            ///< Read cursor: number of bytes already consumed from this block.
        uint32_t wr = 0;            ///< Write cursor: number of bytes committed into this block.
        uint8_t* data() noexcept { return reinterpret_cast<uint8_t*>(this + 1); }
        const uint8_t* data() const noexcept { return reinterpret_cast<const uint8_t*>(this + 1); }
    };

    /**
     * @brief Tagged free-list head: pointer packed with a generation counter in a single 64-bit word.
     *
     * A plain `std::atomic<Block*>` Treiber stack is vulnerable to the ABA
     * problem here: freeing and re-pushing a block between another thread's
     * `head.load()` and its `compare_exchange` can make the CAS succeed
     * against a head pointer that is bitwise-equal but logically stale,
     * corrupting the list. x86-64 user-space pointers fit in 48 bits, so the
     * top 16 bits are free to hold a generation tag bumped on every push;
     * this keeps the tagged head in a native 64-bit word (genuinely
     * lock-free everywhere) instead of a 128-bit struct, which libstdc++
     * only makes lock-free with `-mcx16` (unset in this build) and
     * otherwise silently serialises via `libatomic`.
     */
    struct TaggedHead
    {
        static constexpr uintptr_t kPtrMask = (uintptr_t{1} << 48) - 1;

        uintptr_t bits = 0;

        TaggedHead() noexcept = default;
        TaggedHead(Block* p, uintptr_t tag) noexcept
            : bits((reinterpret_cast<uintptr_t>(p) & kPtrMask) | (tag << 48)) {}

        Block* ptr() const noexcept { return reinterpret_cast<Block*>(bits & kPtrMask); }
        uintptr_t tag() const noexcept { return bits >> 48; }

        bool operator==(const TaggedHead& o) const noexcept { return bits == o.bits; }
    };

    static_assert(std::atomic<TaggedHead>::is_always_lock_free,
                  "TaggedHead must stay a single machine word or the free-list degrades to libatomic locking");

    friend class TxBuffer;

    /// Pops one block from the lock-free free-list, growing the pool if necessary.
    Block* pop() noexcept;

    /// Increments the reference count on @p b by one.
    void addRef(Block* b) noexcept;

    /**
     * @brief Decrements the reference count on @p b and returns it to the free-list when it reaches zero.
     *
     * Safe to call from any thread.
     */
    void release(Block* b) noexcept;

    /// Allocates a new slab of @p blocks blocks and pushes them all onto the free-list.
    void grow(size_t blocks) noexcept;

    /// Atomically pops the head of the given lock-free stack; returns nullptr if empty.
    static Block* atomicPop(std::atomic<TaggedHead>& head) noexcept;

    /// Atomically pushes @p b onto the head of the given lock-free stack.
    static void atomicPush(std::atomic<TaggedHead>& head, Block* b) noexcept;

    PoolConfig& cfg;
    std::atomic<TaggedHead> freeList{}; ///< Lock-free LIFO free-list head, tagged to avoid ABA.
    std::atomic<size_t> totalBlocks{0}; ///< Total number of blocks ever allocated (across all slabs).

    std::mutex growMtx;          ///< Serialises slab growth; not held on the hot pop/release path.
    std::vector<void*> slabs;    ///< Raw slab pointers freed in the destructor; each slab holds slabBlocks blocks.
};
} // namespace transport::tcp

#endif // TCP_TX_BUFFER_POOL_H
