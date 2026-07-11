/**
 * @file RCU.hpp
 * @brief Epoch-based Read-Copy-Update with deferred, function-pointer-based deletion.
 */

#ifndef RCU_HPP
#define RCU_HPP

#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>

namespace utils {

/**
 * @brief Epoch-based RCU: lock-free read-side guards, batched retirement.
 * @ingroup CORE
 *
 * Readers take a @ref Guard (one atomic store + fence on the outermost one;
 * nested guards are a plain increment). Writers unlink an object, then hand it
 * to @ref retire; it is deleted once no reader can still hold a reference.
 *
 * Reclamation rule: an entry retired at epoch E is freed only when
 * `E + 2 <= minActiveEpoch` (minimum over all in-guard threads). This stays
 * correct under any number of concurrent synchronize() calls.
 *
 * Thread registration is automatic on first use; records from exited threads
 * are recycled through a freelist. registerThread()/unregisterThread() are
 * kept for API compatibility.
 *
 * @note retire() is safe to call while holding a Guard: synchronize() skips
 *       the calling thread (the retirer must not touch the object afterward,
 *       which is the contract anyway).
 * @warning A Guard must be created and destroyed on the same thread.
 * @warning retire() blocks (grace period + reclaim) when the ring is full.
 */
class RCU {
public:
    /** @brief Per-thread epoch record, linked into a global list. */
    struct ThreadEpoch {
        std::atomic<uint64_t> epoch{0}; ///< Guard-entry epoch; 0 when quiescent.
        uint32_t nest{0};               ///< Guard nesting depth (owner thread only).
        ThreadEpoch* next{nullptr};     ///< Intrusive list link (immutable once inserted).
        ThreadEpoch* nextFree{nullptr}; ///< Freelist link (guarded by listMtx).
    };

    /** @brief Deferred deletion callback; receives the opaque context pointer. */
    using DeleterFn = void(*)(void* ctx);

private:
    /**
     * @brief Retire-ring entry. The plain fields are protected by the Vyukov
     * `seq` protocol: a producer owns the slot between its head-CAS and the
     * `seq = pos + 1` publish; the single consumer owns it between reading
     * `seq == pos + 1` and recycling with `seq = pos + RETIRE_Q_SIZE`.
     */
    struct RetireEntry
    {
        std::atomic<uint64_t> seq{0};
        uint64_t retireEpoch = 0;
        DeleterFn deleter = nullptr;
        void* context = nullptr;
    };

    static inline std::atomic<uint64_t> globalEpoch{1};
    static inline std::atomic<ThreadEpoch*> head{nullptr};

    static inline std::mutex listMtx;              ///< Guards record insertion + freelist.
    static inline ThreadEpoch* freeHead = nullptr; ///< Records recycled from exited threads.

    static constexpr size_t RETIRE_Q_SIZE = 8192; ///< Must be a power of two.

    static inline std::atomic<uint64_t> retireHead{0}; ///< Producer index (CAS).
    static inline std::atomic<uint64_t> retireTail{0}; ///< Consumer index (reclaimLock holder only).
    static inline std::atomic_flag reclaimLock = ATOMIC_FLAG_INIT;

    struct RetireRing
    {
        RetireEntry entries[RETIRE_Q_SIZE];

        RetireRing() noexcept
        {
            for (size_t i = 0; i < RETIRE_Q_SIZE; ++i)
                entries[i].seq.store(static_cast<uint64_t>(i), std::memory_order_relaxed);
        }
    };

    /** @brief Ring storage; function-local static avoids init-order issues. */
    static RetireEntry* retireRing() noexcept
    {
        static RetireRing ring;
        return ring.entries;
    }

    static size_t ringIndex(uint64_t idx) noexcept
    {
        return static_cast<size_t>(idx) & (RETIRE_Q_SIZE - 1);
    }

    /**
     * @brief Ties a ThreadEpoch record to the thread's lifetime: acquired
     * (recycled or new) on first use, quiesced and returned to the freelist on
     * thread exit. Records never leave the global list, so lock-free traversal
     * stays valid.
     */
    struct Registration
    {
        ThreadEpoch* te;

        Registration()
        {
            std::lock_guard<std::mutex> g(listMtx);
            if (freeHead)
            {
                te = freeHead;
                freeHead = te->nextFree;
                te->nextFree = nullptr;
            }
            else
            {
                te = new ThreadEpoch();
                te->next = head.load(std::memory_order_relaxed);
                head.store(te, std::memory_order_release);
            }
        }

        ~Registration()
        {
            te->nest = 0;
            te->epoch.store(0, std::memory_order_release);
            std::lock_guard<std::mutex> g(listMtx);
            te->nextFree = freeHead;
            freeHead = te;
        }
    };

    /** @brief Thread-local epoch record; registers the thread on first use. */
    static ThreadEpoch& tlsEpoch()
    {
        thread_local Registration reg;
        return *reg.te;
    }

    /** @brief Minimum epoch over all in-guard threads; global epoch if none. */
    static uint64_t minActiveEpoch() noexcept
    {
        uint64_t m = globalEpoch.load(std::memory_order_acquire);
        for (ThreadEpoch* cur = head.load(std::memory_order_acquire); cur; cur = cur->next)
        {
            const uint64_t e = cur->epoch.load(std::memory_order_acquire);
            if (e != 0 && e < m)
                m = e;
        }
        return m;
    }

public:
    /** @brief Explicit registration; kept for API compatibility (now automatic). */
    static void registerThread() noexcept
    {
        (void)tlsEpoch();
    }

    /**
     * @brief Marks the calling thread quiescent; kept for API compatibility.
     * Must not be called while a Guard is active.
     */
    static void unregisterThread() noexcept
    {
        ThreadEpoch& te = tlsEpoch();
        te.nest = 0;
        te.epoch.store(0, std::memory_order_release);
    }

    /**
     * @brief RAII read-side critical section.
     *
     * Only the outermost Guard publishes/clears the thread's epoch; nested
     * Guards bump a counter, so the epoch stays pinned at its entry value for
     * the whole outermost section (required for correctness). Movable so it
     * can live inside snapshot objects; must stay on its creating thread.
     */
    class Guard
    {
    public:
        Guard() noexcept
            : te(&tlsEpoch())
        {
            if (te->nest++ == 0)
            {
                te->epoch.store(globalEpoch.load(std::memory_order_acquire),
                                std::memory_order_relaxed);
                // Store->load barrier: the epoch publish must be visible before
                // any data read inside the critical section, so synchronize()
                // either sees this reader or the reader sees the unlink.
                std::atomic_thread_fence(std::memory_order_seq_cst);
            }
        }

        ~Guard() noexcept
        {
            if (te && --te->nest == 0)
                te->epoch.store(0, std::memory_order_release);
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&& o) noexcept
            : te(o.te)
        {
            o.te = nullptr;
        }

        Guard& operator=(Guard&& o) noexcept
        {
            if (this == &o)
                return *this;

            if (te && --te->nest == 0)
                te->epoch.store(0, std::memory_order_release);

            te = o.te;
            o.te = nullptr;
            return *this;
        }

    private:
        ThreadEpoch* te; ///< Cached record; null for a moved-from Guard.
    };

    /**
     * @brief Advances the global epoch and waits for all *other* active
     * readers to catch up. Skipping the caller makes this safe under a Guard.
     * Intentionally expensive; writers should batch to amortise it.
     */
    static void synchronize() noexcept
    {
        ThreadEpoch* self = &tlsEpoch();

        const uint64_t target = globalEpoch.fetch_add(1, std::memory_order_seq_cst) + 1;

        while (true)
        {
            bool allSafe = true;

            for (ThreadEpoch* cur = head.load(std::memory_order_acquire); cur; cur = cur->next)
            {
                if (cur == self)
                    continue;

                const uint64_t e = cur->epoch.load(std::memory_order_acquire);
                if (e != 0 && e < target)
                {
                    allSafe = false;
                    break;
                }
            }

            if (allSafe)
                break;

            std::this_thread::yield();
        }

        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    /**
     * @brief Runs pending deleters whose retirement epoch is now safe.
     *
     * One reclaimer at a time (try-lock; concurrent callers return at once).
     * Safe standalone — the min-epoch rule needs no preceding synchronize().
     */
    static void tryReclaim() noexcept
    {
        if (reclaimLock.test_and_set(std::memory_order_acquire))
            return;

        RetireEntry* ring = retireRing();
        const uint64_t minEpoch = minActiveEpoch();

        uint64_t pos = retireTail.load(std::memory_order_relaxed);
        while (true)
        {
            RetireEntry& e = ring[ringIndex(pos)];

            if (e.seq.load(std::memory_order_acquire) != pos + 1)
                break; // Empty, or producer not finished publishing.

            if (e.retireEpoch + 2 > minEpoch)
                break; // A reader may still hold a reference.

            const DeleterFn fn = e.deleter;
            void* const ctx = e.context;

            // Recycle the slot before invoking so producers regain space even
            // if the deleter is slow.
            e.seq.store(pos + RETIRE_Q_SIZE, std::memory_order_release);
            ++pos;
            retireTail.store(pos, std::memory_order_release);

            fn(ctx);
        }

        reclaimLock.clear(std::memory_order_release);
    }

    /**
     * @brief Enqueues a deletion callback to be invoked once safe.
     *
     * Lock-free fast path; blocks on a full ring. Safe under a Guard.
     */
    static void retire(DeleterFn fn, void* ctx) noexcept
    {
        RetireEntry* ring = retireRing();

        uint64_t pos = retireHead.load(std::memory_order_relaxed);
        while (true)
        {
            RetireEntry& e = ring[ringIndex(pos)];
            const uint64_t seq = e.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (dif == 0)
            {
                if (retireHead.compare_exchange_weak(pos, pos + 1,
                                                     std::memory_order_acquire,
                                                     std::memory_order_relaxed))
                {
                    e.deleter = fn;
                    e.context = ctx;
                    e.retireEpoch = globalEpoch.load(std::memory_order_acquire);
                    e.seq.store(pos + 1, std::memory_order_release);
                    break;
                }
            }
            else if (dif < 0)
            {
                // Ring full: force a grace period, reclaim, retry.
                synchronize();
                tryReclaim();
                pos = retireHead.load(std::memory_order_relaxed);
            }
            else
            {
                pos = retireHead.load(std::memory_order_relaxed);
            }
        }

        // Periodic reclamation to bound queue growth.
        static thread_local uint32_t counter = 0;
        if (++counter % 128 == 0)
        {
            synchronize();
            tryReclaim();
        }
    }

    /** @brief Retires an object for deletion via plain `delete`. */
    template<typename T>
    static void retireObject(T* ptr) noexcept
    {
        static constexpr DeleterFn deleter = [](void* ctx)
        {
            delete static_cast<T*>(ctx);
        };
        retire(deleter, static_cast<void*>(ptr));
    }
};

} // namespace utils

#endif // RCU_HPP
