/**
 * @file RCU.hpp
 * @brief Read-Copy-Update synchronization for lock-free reads.
 */

#ifndef RCU_HPP
#define RCU_HPP

#include <atomic>
#include <thread>
#include <functional>

namespace utils
{

/**
 * @brief Epoch-based Read-Copy-Update (RCU) primitive for lock-free read paths.
 * @ingroup UTILS
 *
 * Provides a classic RCU contract: readers run without any locks by
 * stamping the current global epoch on entry and clearing it on exit.
 * Writers call synchronize() to advance the epoch and spin until every
 * active reader has observed the new epoch, guaranteeing that no reader
 * still holds a reference to pre-update data.
 *
 * Objects that are no longer reachable from the current version of a
 * data structure are submitted via retire(), which defers their deletion
 * until they are provably invisible to all existing readers.
 *
 * ## Architectural Role
 * Intended for data structures that are read on the fast path far more
 * often than they are modified. The routing table, ARP/NDP caches, and
 * similar hot-read structures use this to avoid reader-side locking
 * entirely. Writers pay the cost of synchronize() on the slow path.
 *
 * ## Concurrency Model
 * - @ref globalEpoch and @ref head are process-global atomics shared by all
 *   threads; no additional lock guards them.
 * - Each thread has a TLS @ref ThreadEpoch that records whether it is
 *   inside a read-side critical section and, if so, which epoch it entered.
 * - The retire queue (@ref retireRight) is a lock-free bounded ring accessed
 *   with CAS on @ref retireHead. Only @ref retireTail is updated under the
 *   implicit serialization provided by synchronize().
 *
 * ## Fast Path vs. Slow Path
 * - Fast path: constructing a @ref Guard (two atomic stores, no blocking).
 * - Slow path: synchronize() spins over all registered thread epochs. Every
 *   128 retirements it also calls synchronize() + tryReclaim() automatically.
 *
 * @warning Every thread that enters a read-side critical section must call
 * registerThread() exactly once before constructing its first Guard.
 * Failure to do so means the thread's epoch is never visible to
 * synchronize(), which can allow a writer to free memory still in use.
 *
 * @warning The retire queue has a fixed capacity of RETIRE_Q_SIZE entries.
 * If the queue fills, retire() calls synchronize() + tryReclaim() inline,
 * which will block the calling thread until space is reclaimed.
 */
class RCU
{
public:
    /**
     * @brief Per-thread epoch record threaded into the global linked list.
     * @ingroup UTILS
     *
     * Threads write their current epoch into this structure on Guard
     * construction so that synchronize() can observe them. The @ref active
     * flag distinguishes a thread that is registered but idle from one
     * that is inside a critical section.
     */
    struct ThreadEpoch
    {
        std::atomic<uint64_t> epoch{0};  ///< Epoch at which this thread entered its current critical section; 0 when idle.
        std::atomic<bool> active{false}; ///< True while the thread is registered with the RCU subsystem.
        ThreadEpoch* next{nullptr};      ///< Intrusive linked-list link; immutable once inserted.
    };

    /**
     * @brief Returns the TLS epoch record for the calling thread, creating it on first call.
     * @ingroup UTILS
     *
     * The returned reference is valid for the lifetime of the thread.
     * The record is heap-allocated once and leaked intentionally; the OS
     * reclaims it on thread exit.
     */
    static ThreadEpoch& tlsEpoch()
    {
        thread_local ThreadEpoch* te = new ThreadEpoch();
        return *te;
    }

private:
    static inline std::atomic<uint64_t> globalEpoch{1};    ///< Monotonically increasing epoch; advanced by each synchronize() call.
    static inline std::atomic<ThreadEpoch*> head{nullptr}; ///< Head of the intrusive list of all registered thread epochs.

    /**
     * @brief A single deferred-deletion entry in the retire ring.
     */
    struct RetireEntry
    {
        uint64_t retireEpoch;         ///< Epoch at which this entry was submitted; safe to reclaim once globalEpoch > retireEpoch + 1.
        std::function<void()> deleter; ///< Callable that frees or otherwise disposes of the retired object.
    };

    static constexpr size_t RETIRE_Q_SIZE = 8192; ///< Maximum number of in-flight retired objects before reclaim is forced.

    static inline std::atomic<size_t> retireHead{0};         ///< Producer index into the retire ring (CAS-updated).
    static inline std::atomic<size_t> retireTail{0};         ///< Consumer index into the retire ring (updated by tryReclaim()).
    static inline RetireEntry retireRight[RETIRE_Q_SIZE];    ///< Fixed-size retire ring buffer.

public:
    /**
     * @brief Registers the calling thread with the RCU subsystem.
     * @ingroup UTILS
     *
     * Inserts the thread's @ref ThreadEpoch into the global list so that
     * synchronize() can observe it. This must be called once per thread
     * before constructing any @ref Guard. Calling it more than once on the
     * same thread is safe; subsequent calls only set the active flag.
     */
    static void registerThread()
    {
        ThreadEpoch* te = &tlsEpoch();
        te->active.store(true, std::memory_order_release);

        static thread_local bool inserted = false;
        if (!inserted)
        {
            ThreadEpoch* old = head.load(std::memory_order_acquire);
            do {
                te->next = old;
            } while (!head.compare_exchange_weak(old, te, std::memory_order_release, std::memory_order_acquire));
            inserted = true;
        }
    }

    /**
     * @brief Deregisters the calling thread and waits for all readers to drain.
     *
     * Marks the thread as inactive, then calls synchronize() to ensure that
     * any objects retired by this thread are safe to reclaim before the
     * thread's stack and TLS are torn down.
     *
     * @warning Must be called before a registered thread exits. Skipping this
     * call leaves a stale, potentially dangling ThreadEpoch in the global list.
     */
    static void unregisterThread()
    {
        ThreadEpoch& te = tlsEpoch();
        te.active.store(false, std::memory_order_release);
        synchronize();
        te.epoch.store(UINT64_MAX, std::memory_order_release);
    }

    /**
     * @brief RAII read-side critical section guard.
     *
     * Construction stamps the calling thread's epoch with the current global
     * epoch, marking it as inside a critical section. Destruction clears the
     * epoch, signalling to synchronize() that this thread holds no references
     * to pre-update data.
     *
     * Guards must not be held across synchronize() calls — doing so would
     * cause synchronize() to spin indefinitely.
     *
     * @warning Do not hold a Guard across a call to synchronize() or
     * unregisterThread() from the same thread.
     */
    class Guard
    {
    public:
        /**
         * @brief Enters the read-side critical section by recording the current epoch.
         */
        explicit Guard() noexcept
        {
            tlsEpoch().epoch.store(globalEpoch.load(std::memory_order_acquire), std::memory_order_release);
        }

        /**
         * @brief Exits the read-side critical section by clearing the epoch.
         */
        ~Guard() noexcept
        {
            tlsEpoch().epoch.store(0, std::memory_order_release);
        }
    };

    /**
     * @brief Advances the global epoch and blocks until all active readers have caught up.
     *
     * After this call returns, no thread can hold a reference to any object
     * that was logically removed before the call. It is safe to free such
     * objects on return.
     *
     * This is the write-side quiescent-state barrier; it is intentionally
     * expensive. Writers should batch updates where possible to amortize the
     * cost.
     */
    static void synchronize() noexcept
    {
        const uint64_t target = globalEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;

        while (true)
        {
            bool allSafe = true;
            ThreadEpoch* cur = head.load(std::memory_order_acquire);

            while (cur)
            {
                if (cur->active.load(std::memory_order_acquire))
                {
                    uint64_t e = cur->epoch.load(std::memory_order_acquire);
                    if (e != 0 && e < target)
                    {
                        allSafe = false;
                        break;
                    }
                }
                cur = cur->next;
            }

            if (allSafe) break;
            std::this_thread::yield();
        }

        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    /**
     * @brief Runs pending deleters whose retire epoch is old enough to be safe.
     *
     * Walks the tail of the retire ring and invokes each deleter whose
     * @c retireEpoch is at least two epochs behind the current global epoch,
     * guaranteeing no active reader can still observe the retired object.
     * Stops at the first entry that is not yet safe to reclaim.
     */
    static void tryReclaim() noexcept
    {
        const uint64_t safeEpoch = globalEpoch.load(std::memory_order_acquire) - 2;
        size_t tail = retireTail.load(std::memory_order_acquire);
        size_t headIdx = retireHead.load(std::memory_order_acquire);

        while (tail != headIdx)
        {
            RetireEntry& e = retireRight[tail % RETIRE_Q_SIZE];
            if (e.retireEpoch == 0 || e.retireEpoch > safeEpoch)
                break;

            auto fn = std::move(e.deleter);
            e.retireEpoch = 0;
            fn();
            ++tail;
        }

        retireTail.store(tail, std::memory_order_acquire);
    }

    /**
     * @brief Submits a deletion callback to be executed once all current readers have finished.
     *
     * The callable @p fn will be invoked by a future call to tryReclaim() once
     * the global epoch has advanced far enough that no reader can still hold a
     * reference to the object being freed. Typically @p fn is a lambda that
     * calls @c delete on a raw pointer.
     *
     * Every 128 calls, retire() implicitly calls synchronize() + tryReclaim()
     * to prevent unbounded queue growth.
     *
     * @tparam F  Callable type. Must be invocable with no arguments.
     * @param fn  Deletion callback forwarded into the retire queue entry.
     *
     * @warning If the retire queue is full, retire() will call synchronize()
     * inline, blocking the caller until space is reclaimed. Avoid retiring
     * objects in tight loops on the fast path.
     */
    template<typename F>
    static void retire(F&& fn) noexcept
    {
        const uint64_t nowEpoch = globalEpoch.load(std::memory_order_acquire);
        size_t headIdx;

        while (true)
        {
            headIdx = retireHead.load(std::memory_order_acquire);
            size_t nextIdx = headIdx + 1;
            size_t tail = retireTail.load(std::memory_order_acquire);

            if (nextIdx - tail >= RETIRE_Q_SIZE)
            {
                synchronize();
                tryReclaim();
                continue;
            }

            if (retireHead.compare_exchange_weak(headIdx, nextIdx, std::memory_order_acq_rel))
                break;
        }

        retireRight[headIdx % RETIRE_Q_SIZE] = { nowEpoch, std::forward<F>(fn) };

        static thread_local size_t counter = 0;
        if (++counter % 128 == 0)
        {
            synchronize();
            tryReclaim();
        }
    }
};

} // namespace utils

#endif // RCU_HPP
