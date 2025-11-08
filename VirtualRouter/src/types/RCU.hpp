// RCU.hpp

#ifndef RCU_HPP
#define RCU_HPP

#include <atomic>
#include <thread>
#include <functional>
#include <vector>
#include <chrono>

class RCU
{
public:
    struct ThreadEpoch
    {
        std::atomic<uint64_t> epoch{0};
        ThreadEpoch* next{nullptr};
    };

private:
    static inline std::atomic<uint64_t> globalEpoch{1};
    static inline std::atomic<ThreadEpoch*> head{nullptr};

    struct RetireEntry
    {
        uint64_t retireEpoch;
        std::function<void()> deleter;
    };

    static constexpr size_t RETIRE_Q_SIZE = 8192;
    static inline std::atomic<size_t> retireHead{0};
    static inline std::atomic<size_t> retireTail{0};
    static inline RetireEntry retireRight[RETIRE_Q_SIZE];

public:
    static ThreadEpoch* registerThread()
    {
        auto* te = new ThreadEpoch();
        ThreadEpoch* oldHead = head.load(std::memory_order_relaxed);
        do
        {
            te->next = oldHead;
        }
        while (!head.compare_exchange_weak(oldHead, te, std::memory_order_release, std::memory_order_relaxed));
        return te;
    }

    static void unregisterThread(ThreadEpoch* te)
    {
        ThreadEpoch* prev = nullptr;
        ThreadEpoch* cur = head.load(std::memory_order_acquire);

        while (cur)
        {
            if (cur == te)
            {
                ThreadEpoch* next = cur->next;
                if (prev)
                    prev->next = next;
                else
                    head.store(next, std::memory_order_release);
                delete te;
                return;
            }
            prev = cur;
            cur = cur->next;
        }
    }

    class Guard
    {
        ThreadEpoch* local;
    public:
        explicit Guard(ThreadEpoch* t) noexcept : local(t)
        {
            local->epoch.store(globalEpoch.load(std::memory_order_acquire), std::memory_order_release);
        }

        ~Guard() noexcept
        {
            local->epoch.store(0, std::memory_order_release);
        }
    };

    static void synchronize() noexcept
    {
        const uint64_t target = globalEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;

        while (true)
        {
            bool allSafe = true;
            ThreadEpoch* cur = head.load(std::memory_order_acquire);

            while (cur)
            {
                const uint64_t e = cur->epoch.load(std::memory_order_acquire);
                if (e != 0 && e < target)
                {
                    allSafe = false;
                    break;
                }
                cur = cur->next;
            }

            if (allSafe) break;
            std::this_thread::yield();
        }

        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

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

#endif // RCU_HPP
