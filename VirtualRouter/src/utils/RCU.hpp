
/**
 * @file RCU.hpp
 * @brief Epoch-based Read-Copy-Update with zero‑overhead deferred deletion.
 *
 * Provides lock‑free read paths and batched, function‑pointer‑based retirement.
 * All type erasure is removed – deleters are invoked directly via raw pointers.
 */

#ifndef RCU_HPP
#define RCU_HPP

#include <atomic>
#include <thread>
#include <cstdint>

namespace utils {

/**
 * @brief Epoch‑based RCU primitive for lock‑free reads with efficient retirement.
 *
 * ## Design Goals
 * - **Reader‑side**: two atomic stores per Guard (minimal overhead).
 * - **Writer‑side**: synchronize() advances epoch and waits for quiescent state.
 * - **Deferred deletion**: uses a fixed‑size ring buffer of `(epoch, fn, ctx)`
 *   triples. No `std::function`, no heap allocation for callbacks.
 *
 * ## Fast vs. Slow Path
 * - **Fast path** (`Guard`): atomic loads/stores, no blocking.
 * - **Slow path** (`synchronize`): spins on thread epoch list; intended for
 *   infrequent writers.
 * - **Reclamation** (`retire`): lock‑free CAS to enqueue a deletion request.
 *   Every 128th call triggers an automatic `synchronize()+tryReclaim()` to
 *   bound queue growth.
 *
 * ## Memory Ordering
 * All operations use acquire/release semantics where necessary to ensure
 * visibility of updates across threads. The `globalEpoch` is monotonic and
 * serves as a logical timestamp.
 *
 * @warning Every thread that enters a read‑side critical section must call
 *          `registerThread()` exactly once before its first `Guard`.
 * @warning The retire queue has fixed capacity (`RETIRE_Q_SIZE`). If full,
 *          `retire()` will call `synchronize()` inline and block until space
 *          is reclaimed. Do not call `retire()` from real‑time threads that
 *          cannot tolerate blocking.
 */
class RCU {
public:
    /**
     * @brief Per‑thread epoch record, linked into a global list.
     *
     * Threads set `epoch` to the current global value when inside a critical
     * section, and clear it (to 0) on exit. The `active` flag indicates that
     * the thread is registered and should be considered by `synchronize()`.
     */
    struct ThreadEpoch {
        std::atomic<uint64_t> epoch{0};   ///< Current epoch if in a guard; 0 otherwise.
        std::atomic<bool> active{false};  ///< True after `registerThread()`.
        ThreadEpoch* next{nullptr};       ///< Intrusive list link.
    };

    /**
     * @brief Returns the thread‑local epoch record, allocated once per thread.
     *
     * The record is intentionally leaked – the OS reclaims it on thread exit.
     */
    static ThreadEpoch& tlsEpoch()
    {
        thread_local ThreadEpoch* te = new ThreadEpoch();
        return *te;
    }

    /**
     * @brief Signature for a deferred deletion callback.
     *
     * @param ctx  Opaque pointer passed to the deleter (e.g., the object to free).
     */
    using DeleterFn = void(*)(void* ctx);

private:
    /**
     * @brief A single entry in the retire ring buffer.
     *
     * Uses raw function pointer
     * This eliminates heap allocation, virtual calls, and type erasure.
     */
    struct RetireEntry
    {
        uint64_t retireEpoch;   ///< Epoch when this entry was enqueued.
        DeleterFn deleter;      ///< Function to call (pure pointer, no indirection).
        void* context;          ///< Argument passed to deleter.

        RetireEntry() : retireEpoch(0), deleter(nullptr), context(nullptr) {}
    };

    static inline std::atomic<uint64_t> globalEpoch{1};      ///< Monotonic epoch counter.
    static inline std::atomic<ThreadEpoch*> head{nullptr};   ///< Linked list head.

    static constexpr size_t RETIRE_Q_SIZE = 8192;            ///< Must be power of two for fast modulo.

    static inline std::atomic<size_t> retireHead{0};         ///< Producer index (CAS).
    static inline std::atomic<size_t> retireTail{0};         ///< Consumer index (CAS).
    static inline RetireEntry retireRight[RETIRE_Q_SIZE];    ///< Ring buffer.

    /**
     * @brief Fast modulo for power‑of‑two sizes.
     */
    static inline size_t ringIndex(size_t idx) noexcept
    {
        return idx & (RETIRE_Q_SIZE - 1);
    }

public:
    /**
     * @brief Registers the calling thread with the RCU subsystem.
     *
     * Inserts the thread's `ThreadEpoch` into the global list and marks it active.
     * This must be called before constructing any `Guard`. Multiple calls are safe.
     */
    static void registerThread() noexcept
    {
        ThreadEpoch* te = &tlsEpoch();
        te->active.store(true, std::memory_order_release);

        static thread_local bool inserted = false;
        if (!inserted)
        {
            ThreadEpoch* old = head.load(std::memory_order_acquire);
            do
            {
                te->next = old;
            }
            while (!head.compare_exchange_weak(old, te,
                                                 std::memory_order_release,
                                                 std::memory_order_acquire));
            inserted = true;
        }
    }

    /**
     * @brief Deregisters the calling thread and waits for all readers to finish.
     *
     * Marks the thread inactive, then calls `synchronize()` to ensure any
     * objects retired by this thread are safe to reclaim. Must be called
     * before the thread exits.
     */
    static void unregisterThread() noexcept
    {
        ThreadEpoch& te = tlsEpoch();
        te.active.store(false, std::memory_order_release);
        synchronize();
        te.epoch.store(0, std::memory_order_release);
    }

    /**
     * @brief RAII guard for a read‑side critical section.
     *
     * Construction records the current global epoch in the thread's TLS.
     * Destruction clears it. Guards must not be held across a call to
     * `synchronize()` (would cause writer to spin).
     */
    class Guard
    {
    public:
        explicit Guard() noexcept
        {
            // Acquire load of globalEpoch ensures we see all previous writes.
            uint64_t epoch = globalEpoch.load(std::memory_order_acquire);
            tlsEpoch().epoch.store(epoch, std::memory_order_release);
        }

        ~Guard() noexcept
        {
            // Release store ensures that the epoch clear is visible to synchronize.
            if (owns) tlsEpoch().epoch.store(0, std::memory_order_release);
        }

        // Non‑copyable, non‑movable
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&& o)
        {
            o.owns = false;
        }
        Guard& operator=(Guard&& o)
        {
            o.owns = false;
            return *this;
        }

    private:
        bool owns = true;
    };

    /**
     * @brief Advances the global epoch and waits for all active readers to catch up.
     *
     * This is the writer’s quiescent‑state barrier. It is intentionally expensive;
     * writers should batch updates to amortise the cost.
     */
    static void synchronize() noexcept
    {
        // Atomically increment epoch and get the new value.
        const uint64_t target = globalEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;

        // Spin until every registered thread either has epoch >= target or is idle.
        while (true)
        {
            bool allSafe = true;
            ThreadEpoch* cur = head.load(std::memory_order_acquire);

            while (cur)
            {
                // Only consider active threads; inactive ones are safe.
                if (cur->active.load(std::memory_order_acquire))
                {
                    uint64_t e = cur->epoch.load(std::memory_order_acquire);
                    // If a thread is inside a guard with an epoch older than target,
                    // it might still hold references to old data.
                    if (e != 0 && e < target)
                    {
                        allSafe = false;
                        break;
                    }
                }
                cur = cur->next;
            }

            if (allSafe) break;
            std::this_thread::yield();  // Back off to reduce CPU contention.
        }

        // Full fence ensures all prior memory operations are visible to subsequent
        // reads that rely on this barrier.
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    /**
     * @brief Runs pending deleters whose retirement epoch is now safe.
     *
     * Scans the retire ring buffer from the current tail forward, reclaiming
     * any entry whose `retireEpoch <= globalEpoch - 2`. Uses a CAS on
     * `retireTail` to claim a contiguous block, ensuring each entry is deleted
     * exactly once.
     */
    static void tryReclaim() noexcept
    {
        uint64_t curEpoch = globalEpoch.load(std::memory_order_acquire);
        if (curEpoch < 2) return;  // Not enough epochs to safely reclaim anything.

        const uint64_t safeEpoch = curEpoch - 2;  // Safe when globalEpoch > retireEpoch + 1.

        while (true)
        {
            size_t tail = retireTail.load(std::memory_order_acquire);
            size_t headIdx = retireHead.load(std::memory_order_acquire);
            size_t newTail = tail;

            // Walk the ring until we hit an entry that is not safe.
            while (newTail != headIdx)
            {
                RetireEntry& e = retireRight[ringIndex(newTail)];
                if (e.retireEpoch == 0 || e.retireEpoch > safeEpoch)
                    break;
                ++newTail;
            }

            if (newTail == tail) break;  // No work.

            // Attempt to atomically advance the tail to newTail.
            if (retireTail.compare_exchange_weak(tail, newTail,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
                // We own the range [tail, newTail). Invoke deleters.
                for (size_t i = tail; i < newTail; ++i)
                {
                    RetireEntry& e = retireRight[ringIndex(i)];
                    DeleterFn fn = e.deleter;
                    void* ctx = e.context;
                    e.retireEpoch = 0;      // Mark as reclaimed.
                    e.deleter = nullptr;
                    e.context = nullptr;
                    fn(ctx);                // Direct function call, zero overhead.
                }
                break;
            }
            // CAS failed; another thread claimed the range. Retry.
        }
    }

    /**
     * @brief Enqueues a deletion callback to be invoked when safe.
     *
     * This is the primary API for retiring objects. The callback is a raw
     * function pointer that receives a void* context. No heap allocation occurs.
     *
     * @param fn     Function to call (e.g., a custom deleter).
     * @param ctx    Opaque pointer passed to `fn` (typically the object to delete).
     */
    static void retire(DeleterFn fn, void* ctx) noexcept
    {
        const uint64_t nowEpoch = globalEpoch.load(std::memory_order_acquire);

        while (true)
        {
            size_t headIdx = retireHead.load(std::memory_order_acquire);
            size_t nextIdx = headIdx + 1;
            size_t tail = retireTail.load(std::memory_order_acquire);

            // Check if the ring buffer is full.
            if (nextIdx - tail >= RETIRE_Q_SIZE)
            {
                synchronize();
                tryReclaim();
                continue;  // Retry after freeing space.
            }

            // Attempt to claim the next slot.
            if (retireHead.compare_exchange_weak(headIdx, nextIdx,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
                // Success; fill the entry.
                RetireEntry& e = retireRight[ringIndex(headIdx)];
                e.retireEpoch = nowEpoch;
                e.deleter = fn;
                e.context = ctx;
                break;
            }
        }

        // Auto‑reclaim every 128 retirements to bound queue growth.
        static thread_local size_t counter = 0;
        if (++counter % 128 == 0)
        {
            synchronize();
            tryReclaim();
        }
    }

    /**
     * @brief Convenience template to retire an object using its normal `delete`.
     *
     * This is a zero‑overhead wrapper around `retire(DeleterFn, void*)`.
     * It creates a static deleter function that casts the void* back to T*
     * and calls `delete`. Because the deleter is a plain function, there is
     * no per‑call type erasure overhead.
     *
     * @tparam T      Type of the object to delete.
     * @param ptr     Pointer to the object (must have been allocated with `new`).
     */
    template<typename T>
    static void retireObject(T* ptr) noexcept
    {
        // Static deleter function – only one instance per T.
        static DeleterFn deleter = [](void* ctx)
        {
            delete static_cast<T*>(ctx);
        };
        retire(deleter, static_cast<void*>(ptr));
    }
};

} // namespace utils

#endif // RCU_HPP
