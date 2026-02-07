#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <algorithm>    // std::max
#include <immintrin.h>  // _mm_pause
#include <RCU.hpp>

class ThreadPool
{
public:
    // Small-inline functor storage: no std::function, no allocations per task.
    struct Task
    {
        using InvokeFn  = void(*)(void*);
        using DestroyFn = void(*)(void*);

        alignas(64) unsigned char storage[64];
        InvokeFn  invoke  = nullptr;
        DestroyFn destroy = nullptr;

        Task() = default;

        template<typename F>
        void set(F&& f)
        {
            using Fn = typename std::decay<F>::type;
            static_assert(sizeof(Fn) <= sizeof(storage), "Lambda too large for inline storage");
            new (storage) Fn(std::forward<F>(f));
            invoke  = [](void* p){ (*reinterpret_cast<Fn*>(p))(); };
            destroy = [](void* p){ reinterpret_cast<Fn*>(p)->~Fn(); };
        }

        // Run lambda if present
        void run() noexcept
        {
            if (invoke) invoke(storage);
        }
        // Destroy lambda if present
        void cleanup() noexcept
        {
            if (destroy) destroy(storage);
            invoke = nullptr;
            destroy = nullptr;
        }

        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;
        ~Task()                      = default; // never auto-destroy per-slot; we manage it explicitly
    };

    // numThreads: worker count
    // capacity:   queue capacity (must be power of two)
    explicit ThreadPool(size_t numThreads = std::max(4u, std::thread::hardware_concurrency()),
                        size_t capacity   = (1u << 16))
        : capacity_(capacity),
          mask_(capacity - 1),
          head_(0),
          tail_(0),
          stop_(false)
    {
        if (capacity_ < 2 || (capacity_ & mask_) != 0)
            throw std::runtime_error("ThreadPool capacity must be a power of 2 and >= 2");

        slots_ = new Slot[capacity_];
        // Initialize per-slot sequence numbers
        for (size_t i = 0; i < capacity_; ++i)
            slots_[i].seq.store(static_cast<uint64_t>(i), std::memory_order_relaxed);

        // Launch workers
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i)
            workers_.emplace_back([this]{ workerLoop(); });
    }

    ~ThreadPool()
    {
        shutdown();
        delete[] slots_;
    }

    // Enqueue a lambda (no args; captures only). Returns false if queue is full.
    template<typename F>
    bool enqueue(F&& f)
    {
        uint64_t pos = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            Slot* s = &slots_[pos & mask_];
            uint64_t seq = s->seq.load(std::memory_order_acquire);
            intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0)
            {
                // try to claim this sequence number
                if (head_.compare_exchange_weak(pos, pos + 1,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
                {
                    s->task.set(std::forward<F>(f));           // construct lambda
                    s->seq.store(pos + 1, std::memory_order_release); // publish
                    return true;
                }
                // CAS failed, pos updated by other producer; retry
            }
            else if (dif < 0)
            {
                // seq < pos -> slot not yet recycled => queue full
                return false;
            }
            else
            {
                // Another producer advanced this slot; reload head and retry
                pos = head_.load(std::memory_order_relaxed);
            }
            _mm_pause();
        }
    }

    void shutdown()
    {
        bool expected = false;
        if (stop_.compare_exchange_strong(expected, true, std::memory_order_release))
        {
            // join once
            for (auto& t : workers_)
                if (t.joinable()) t.join();
        }
    }

private:
    struct Slot
    {
        // Sequence number protocol:
        //  producer owns slot when seq == index
        //  consumer owns slot when seq == index + 1
        std::atomic<uint64_t> seq;
        Task task;
    };

    void workerLoop()
    {
        RCU::registerThread();
        while (!stop_.load(std::memory_order_acquire))
        {
            if (consumeOne())
                continue;

            // light backoff when empty
            for (int i = 0; i < 64 && !stop_.load(std::memory_order_relaxed); ++i)
                _mm_pause();
        }

        // Drain remaining tasks
        while (consumeOne()) {}
        RCU::unregisterThread();
    }

    bool consumeOne()
    {
        uint64_t pos = tail_.load(std::memory_order_relaxed);
        for (;;)
        {
            Slot* s = &slots_[pos & mask_];
            uint64_t seq = s->seq.load(std::memory_order_acquire);
            intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (dif == 0)
            {
                // try to claim this item
                if (tail_.compare_exchange_weak(pos, pos + 1,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
                {
                    // We own the slot now
                    s->task.run();      // execute
                    s->task.cleanup();  // destroy captures + reset
                    // recycle slot for producers: set seq = pos + capacity
                    s->seq.store(pos + capacity_, std::memory_order_release);
                    return true;
                }
                // CAS failed, pos updated by other consumer; retry with new pos
            }
            else if (dif < 0)
            {
                // Empty at this pos
                return false;
            }
            else
            {
                // Another consumer advanced; reload tail and retry
                pos = tail_.load(std::memory_order_relaxed);
            }
            _mm_pause();
        }
    }

    // queue
    const size_t capacity_;
    const size_t mask_;
    Slot* slots_;

    // indices
    alignas(64) std::atomic<uint64_t> head_; // producer index
    alignas(64) std::atomic<uint64_t> tail_; // consumer index

    // control
    std::atomic<bool> stop_;

    // workers
    std::vector<std::thread> workers_;

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};

#endif // THREADPOOL_HPP

