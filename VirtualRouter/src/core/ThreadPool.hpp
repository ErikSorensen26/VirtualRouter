/**
 * @file ThreadPool.hpp
 * @brief Lock-free MPMC thread pool with inline lambda storage.
 */

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <immintrin.h>
#include <RCU.hpp>

namespace core
{

/**
 * @brief Lock-free MPMC thread pool backed by a power-of-two ring of inline task slots.
 * @ingroup CORE
 *
 * `ThreadPool` provides a fixed set of worker threads and a bounded, lock-free
 * multi-producer/multi-consumer queue.  Lambdas are stored inline inside each ring
 * slot (up to 128 bytes) to avoid per-task heap allocation.
 *
 * ## Architectural Role
 * - All protocol timers and control-plane tasks ultimately land here via
 *   @ref ControlScheduler, which serializes per-queue work before submitting to the pool.
 * - Hardware RX/TX paths may also submit lightweight tasks here for off-loading.
 *
 * ## Lifecycle & Ownership
 * - Owned by @ref Global; one pool is shared across the entire process.
 * - Workers are launched in the constructor and joined in `shutdown()` / destructor.
 *
 * ## Concurrency Model
 * - `enqueue()` is safe to call from any thread simultaneously; uses CAS on
 *   the head index to claim a slot without a mutex.
 * - Workers use an analogous CAS on the tail index to claim tasks.
 *
 * ## Fast Path vs. Slow Path
 * - **Fast path**: `enqueue()` is a tight CAS loop with `_mm_pause` backoff — no locks,
 *   no allocations.
 * - **Slow path**: `shutdown()` joins all worker threads, which may block.
 *
 * @warning The queue is bounded.  `enqueue()` returns `false` when full; callers
 * must handle backpressure (e.g., spin or drop the task).
 */
class ThreadPool
{
public:
    /**
     * @brief Inline functor storage that avoids heap allocation per task.
     * @ingroup CORE
     *
     * Each ring slot owns one `Task`.  `set()` placement-new's the lambda into
     * the inline storage buffer.  `run()` invokes it and `cleanup()` destructs it
     * and resets the function pointers so the slot is ready for reuse.
     *
     * @warning Lambdas larger than 128 bytes trigger a `static_assert` at compile time.
     */
    struct Task
    {
        using InvokeFn  = void(*)(void*); ///< Type-erased invocation trampoline.
        using DestroyFn = void(*)(void*); ///< Type-erased destructor trampoline.

        alignas(64) unsigned char storage[128]; ///< Inline storage for the captured lambda.
        InvokeFn  invoke  = nullptr; ///< Points to the lambda's call operator; null if empty.
        DestroyFn destroy = nullptr; ///< Points to the lambda's destructor; null if empty.

        Task() = default;

        /**
         * @brief Stores a callable `f` into the inline storage.
         * @ingroup CORE
         *
         * Placement-new's the decayed type into `storage` and wires up the
         * type-erased `invoke` and `destroy` trampolines.
         *
         * @tparam F  Callable type; `sizeof(F)` must be ≤ 128 bytes.
         * @param f   Callable to store (moved into storage).
         */
        template<typename F>
        void set(F&& f)
        {
            using Fn = typename std::decay<F>::type;
            static_assert(sizeof(Fn) <= sizeof(storage), "Lambda too large for inline storage");
            new (storage) Fn(std::forward<F>(f));
            invoke  = [](void* p){ (*reinterpret_cast<Fn*>(p))(); };
            destroy = [](void* p){ reinterpret_cast<Fn*>(p)->~Fn(); };
        }

        /** @brief Executes the stored callable if one is present. */
        void run() noexcept
        {
            if (invoke) invoke(storage);
        }

        /** @brief Destructs the stored callable and resets the slot to empty. */
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

    /**
     * @brief Constructs the pool and starts `numThreads` worker threads.
     *
     * @param numThreads  Number of worker threads.  Defaults to `max(4, hardware_concurrency)`.
     * @param capacity    Ring buffer capacity.  Must be a power of two and ≥ 2.
     *
     * @warning If `capacity` is not a power of two, the constructor throws `std::runtime_error`.
     */
    explicit ThreadPool(size_t numThreads, size_t capacity)
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

    /**
     * @brief Enqueues a zero-argument callable for execution by a worker thread.
     *
     * Uses a CAS-based claim on the head index; no mutex is held.  The lambda is
     * moved into the ring slot's inline storage.
     *
     * @tparam F  Callable type; `sizeof(F)` must be ≤ 128 bytes.
     * @param f   Task to enqueue; must accept no arguments and return void.
     * @return `true` if the task was enqueued; `false` if the ring is full.
     *
     * @note The caller is responsible for handling a `false` return — there is no
     * built-in retry or blocking behavior.
     */
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

    /**
     * @brief Signals all worker threads to stop and joins them.
     *
     * May be called before the destructor to drain the pool while other
     * resources are still live. Safe to call multiple times; subsequent calls
     * are no-ops.
     */
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
    /**
     * @brief Internal ring slot pairing a task with its Vyukov sequence number.
     *
     * The sequence number protocol:
     * - `seq == index`     → slot is empty; a producer may claim it.
     * - `seq == index + 1` → slot is full; a consumer may claim it.
     * - `seq == index + capacity` → slot has been recycled by the consumer.
     */
    struct Slot
    {
        std::atomic<uint64_t> seq; ///< Vyukov sequence number for this slot.
        Task task;                 ///< Inline task storage.
    };

    /**
     * @brief Per-worker event loop: consumes tasks until @c stop_ is set.
     *
     * Registers the thread with @ref utils::RCU on entry and unregisters on exit.
     * Busy-spins with @c _mm_pause backoff when the queue is empty.
     */
    void workerLoop()
    {
        utils::RCU::registerThread();
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
        utils::RCU::unregisterThread();
    }

    /**
     * @brief Attempts to dequeue and execute one task from the ring.
     *
     * Uses a CAS on the tail index to claim a slot. Executes the task in-place
     * and then recycles the slot for producers.
     *
     * @return `true` if a task was consumed; `false` if the ring was empty.
     */
    bool consumeOne()
    {
        uint64_t pos = tail_.load(std::memory_order_relaxed);
        while (true)
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

    // RING STATE
    const size_t capacity_; ///< Ring buffer capacity (power of two).
    const size_t mask_;     ///< `capacity_ - 1`; used for fast modulo.
    Slot* slots_;           ///< Heap-allocated array of ring slots.

    // INDICES (cache-line isolated to prevent false sharing)
    alignas(64) std::atomic<uint64_t> head_; ///< Producer cursor (next slot to claim).
    alignas(64) std::atomic<uint64_t> tail_; ///< Consumer cursor (next slot to consume).

    // CONTROL
    std::atomic<bool> stop_; ///< Set to @c true by @ref shutdown() to halt workers.

    // WORKERS
    std::vector<std::thread> workers_; ///< Worker threads running @ref workerLoop().

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};

} // namespace core

#endif // THREADPOOL_HPP


