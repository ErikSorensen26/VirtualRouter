/**
 * @file ControlScheduler.h
 * @brief Serialized task scheduler with timer support for control-plane protocols.
 */

#ifndef CONTROL_ENGINE_H
#define CONTROL_ENGINE_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <future>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include <cassert>
#include <new>
#include <immintrin.h>

#include <ThreadPool.hpp>
#include <TimeManager.h>

namespace core
{

class ProcessQueueRef;
class ProcessQueue;

/**
 * @brief Intrusive linked-list node tracking an in-flight delayed timer owned by a @ref ProcessQueueRef.
 * @ingroup CORE
 *
 * Nodes are heap-allocated when `ProcessQueueRef::postAfter()` registers a timer and are
 * freed once the timer fires or is cancelled.  The `active` flag lets the cancellation
 * path mark a node without acquiring a lock.
 */
struct RefTimerNode
{
    std::atomic<RefTimerNode*> next{nullptr}; ///< Next node in the owner's timer list.
    std::atomic<bool> active{true};           ///< False once the timer has been cancelled.
    uint32_t handle = 0;                      ///< Public timer handle returned to the caller.
};

/**
 * @brief Shared lifetime-tracking state for a @ref ProcessQueueRef.
 * @ingroup CORE
 *
 * Each `ProcessQueueRef` allocates one of these on construction.  The `alive`
 * flag gates whether posted callbacks are executed after the ref is released.
 * `pending` counts in-flight tasks; the destructor of `ProcessQueueRef` spins
 * until `pending` reaches zero so all lambdas that captured `this` have finished.
 */
struct ProcessQueueRefState
{
    std::atomic<bool> alive{true};             ///< False after the owning ProcessQueueRef is released.
    std::atomic<uint32_t> pending{0};          ///< Count of posted tasks not yet executed.
    std::atomic<RefTimerNode*> timerHead{nullptr}; ///< Head of the singly-linked list of live timers.
    std::atomic<uint32_t> epoch;
};

/**
 * @brief Decrements the pending count on a @ref ProcessQueueRefState and notifies waiters.
 *
 * Called by @ref RefPendingToken on destruction to signal the destructor of
 * @ref ProcessQueueRef that one more in-flight task has completed.
 *
 * @param state  State to update; safe to call with `nullptr` (no-op).
 */
inline void releaseRefPending(ProcessQueueRefState* state) noexcept
{
    if (!state)
        return;

    const uint32_t prev = state->pending.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
        state->pending.notify_all();
}

/**
 * @brief RAII token that decrements the @ref ProcessQueueRefState pending counter on destruction.
 * @ingroup CORE
 *
 * Every lambda posted via @ref ProcessQueueRef::post or @ref ProcessQueueRef::postAfter captures
 * one of these tokens by move.  When the lambda completes (or is discarded), the token destructor
 * decrements `state->pending`, allowing `ProcessQueueRef::release()` to unblock.
 */
struct RefPendingToken
{
    ProcessQueueRefState* state = nullptr;

    RefPendingToken() = default;

    /**
     * @brief Constructs a token that will decrement `s->pending` on destruction.
     * @param s  State object whose `pending` counter was pre-incremented by the caller.
     */
    explicit RefPendingToken(ProcessQueueRefState* s) noexcept
        : state(s)
    {}

    RefPendingToken(const RefPendingToken&) = delete;
    RefPendingToken& operator=(const RefPendingToken&) = delete;

    RefPendingToken(RefPendingToken&& o) noexcept
        : state(o.state)
    {
        o.state = nullptr;
    }

    RefPendingToken& operator=(RefPendingToken&& o) noexcept
    {
        if (this == &o)
            return *this;

        if (state)
            releaseRefPending(state);

        state = o.state;
        o.state = nullptr;
        return *this;
    }

    ~RefPendingToken() noexcept
    {
        if (state)
            releaseRefPending(state);
    }
};

/**
 * @brief Serialized task scheduler that multiplexes control-plane work over a shared @ref ThreadPool.
 * @ingroup CORE
 *
 * `ControlScheduler` provides a pool of individually-serialized execution contexts called
 * @ref ProcessQueue instances.  Each `ProcessQueue` is a single-consumer MPSC ring: only
 * one thread runs its tasks at a time, which allows protocol state machines to be written
 * without internal locking.
 *
 * Each instance contains:
 * - A pool of @ref ProcessQueue slots, each backed by up to 8 sub-queues selectable by label.
 * - A pool of @ref DelayedSlot entries for timer-driven deferred tasks.
 * - A reference to the global @ref ThreadPool for task dispatch.
 * - A reference to the global @ref TimeManager for timer registration.
 *
 * ## Architectural Role
 * `ControlScheduler` sits between protocol state machines and the raw `ThreadPool`.
 * Protocols never submit directly to the pool; they post to a `ProcessQueue` or
 * `ProcessQueueRef`, which guarantees serialization and respects lifetime (`alive` flag).
 *
 * ## Lifecycle & Ownership
 * - Owned by @ref Global; lives for the entire process lifetime.
 * - `ProcessQueue` objects are created via `create()` and destroyed by moving them
 *   out of scope (RAII).  The underlying slot is recycled for the next `create()`.
 * - `ProcessQueueRef` is a borrow of a `ProcessQueue` that adds safe post-destruction
 *   semantics: tasks posted after the ref is released are silently dropped.
 *
 * ## Concurrency Model
 * - `ProcessQueue::post()` is safe to call from any thread.
 * - All tasks posted to the same `ProcessQueue` are executed serially on whatever
 *   thread the pool assigns to drain it; that thread changes between drains.
 * - `pqAllocMtx` guards slot allocation/free only; no lock is held during task execution.
 *
 * ## Fast Path vs. Slow Path
 * - **Fast path**: `post()` enqueues a task and triggers a single pool `enqueue()` if
 *   the queue was idle.  No allocation, no system calls.
 * - **Slow path**: `postAfter()` allocates a `DelayedSlot`, registers a `TimeManager`
 *   timer, and links a `RefTimerNode` if an owner is present.
 *
 * @warning Do not destroy a `ProcessQueue` while tasks are in flight; call
 * `ProcessQueue::reset()` only after all external posters have stopped.
 *
 * @see ProcessQueue
 * @see ProcessQueueRef
 */
class ControlScheduler
{
    static constexpr uint64_t kMaxSubQueues = 8;
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

public:
    using ProcessQueueId = uint32_t; ///< Opaque slot index identifying a @ref ProcessQueue.
    using Label = uint16_t;          ///< Sub-queue selector for priority/class separation.

    /**
     * @brief Configuration for a single labeled sub-queue within a @ref ProcessQueue.
     * @ingroup CORE
     *
     * When creating a queue with `create()`, callers may supply labeled sub-queues to
     * separate high-priority from low-priority work within the same serialized context.
     */
    struct SubQueueConfig
    {
        Label label;       ///< Unique label identifying this sub-queue.
        uint32_t capacity; ///< Ring capacity (must be power of two).
    };

    /**
     * @brief Constructs the scheduler and pre-allocates slot arrays.
     *
     * @param externalPool    ThreadPool used to dispatch drain tasks.
     * @param tmgr            TimeManager used to register delayed-task timers.
     * @param maxQueues       Maximum number of simultaneous @ref ProcessQueue instances.
     * @param maxDelayedTimers Maximum number of in-flight delayed tasks.
     */
    explicit ControlScheduler(core::ThreadPool& externalPool,
                              core::TimeManager& tmgr,
                              size_t maxQueues = 4096,
                              size_t maxDelayedTimers = 4096);

    /**
     * @brief Destructs the scheduler and tears down all remaining queues.
     *
     * Sets the `stopping` flag so that post/schedule calls become no-ops, then
     * waits for all in-flight drain tasks to complete before freeing slot arrays.
     */
    ~ControlScheduler();

    ControlScheduler(const ControlScheduler&) = delete;
    ControlScheduler& operator=(const ControlScheduler&) = delete;

    /** @brief Returns the @ref TimeManager used for delayed task scheduling. */
    core::TimeManager& timers() noexcept { return timeManager; }

    /**
     * @brief Allocates a new serialized @ref ProcessQueue.
     *
     * The returned object owns the slot; dropping or resetting it returns the
     * slot to the free pool.  Sub-queues can be assigned labels so that
     * `post(Label, fn)` routes the task to the correct ring.
     *
     * @param capacity  Default sub-queue ring capacity (must be power of two).
     * @param labeled   Optional additional sub-queues with explicit labels and capacities.
     * @return A new, ready-to-use @ref ProcessQueue.
     */
    ProcessQueue create(uint32_t capacity = 4096,
                        std::initializer_list<SubQueueConfig> labeled = {});

    /**
     * @brief Attempts to cancel a pending delayed task by its timer handle.
     *
     * If the timer has already fired or the handle is stale, this is a no-op
     * and returns `false`.  Safe to call from any thread.
     *
     * @param timerId  Handle returned by `ProcessQueue::schedule()` or
     *                 `ProcessQueueRef::postAfter()`.
     * @return `true` if the timer was found and cancelled before firing.
     */
    bool cancelDelayed(uint32_t timerId) noexcept;

private:
    /**
     * @brief Blocks the calling thread until the named @ref ProcessQueue has no
     *        in-flight or pending tasks.
     *
     * Used by tests to deterministically wait for asynchronous work posted via
     * `EigrpSyncNetworks`-style appliers to finish before inspecting state.
     *
     * @warning Must not be called from within a task running on this queue;
     * doing so deadlocks (the queue can never become idle while it is draining
     * the very task that called this).
     *
     * @param id   Slot index identifying the @ref ProcessQueue.
     * @param gen  Generation counter matching @ref ProcessQueueSlot::generation.
     */
    void waitIdle(ProcessQueueId id, uint32_t gen) noexcept;

private:
    struct SubQueue
    {
        struct Slot
        {
            std::atomic<uint64_t> seq;
            core::ThreadPool::Task task;
        };

        SubQueue() = default;
        ~SubQueue() { reset(); }

        SubQueue(const SubQueue&) = delete;
        SubQueue& operator=(const SubQueue&) = delete;

        void init(uint32_t capacity);
        void reset() noexcept;

        template <typename F>
        bool tryEmplace(F&& f) noexcept
        {
            uint64_t pos = head.load(std::memory_order_relaxed);

            while (true)
            {
                Slot* s = &slots[pos & mask];
                uint64_t seq = s->seq.load(std::memory_order_acquire);
                intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

                if (dif == 0)
                {
                    if (head.compare_exchange_weak(pos, pos + 1, std::memory_order_acquire, std::memory_order_relaxed))
                    {
                        s->task.set(std::forward<F>(f));
                        s->seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (dif < 0)
                {
                    return false;
                }
                else
                {
                    pos = head.load(std::memory_order_relaxed);
                }

                _mm_pause();
            }
        }

        bool tryConsumeOne(bool run) noexcept;
        bool hasItem() const noexcept;

        void discardAll() noexcept
        {
            while (tryConsumeOne(false)) {}
        }

        uint32_t getCapacity() const noexcept { return capacity; }

    private:
        uint32_t capacity = 0;
        uint32_t mask = 0;
        Slot* slots = nullptr;

        alignas(64) std::atomic<uint64_t> head{0};
        alignas(64) std::atomic<uint64_t> tail{0};
    };

    struct LabelMapEntry
    {
        uint16_t label;
        uint16_t subIndex;
    };

    struct ProcessQueueState
    {
        std::atomic<bool> closed{false};
        std::atomic<bool> scheduled{false};
        std::atomic<bool> draining{false};
        std::atomic<void*> drainThreadMarker{nullptr};
        std::atomic<bool> deferDestroy{false};

        std::mutex waitMtx;
        std::condition_variable waitCv;

        uint16_t subCount = 0;
        SubQueue sub[kMaxSubQueues];

        uint16_t labelCount = 0;
        LabelMapEntry labels[kMaxSubQueues - 1];

        void resetConfig() noexcept
        {
            closed.store(false, std::memory_order_relaxed);
            scheduled.store(false, std::memory_order_relaxed);
            draining.store(false, std::memory_order_relaxed);
            drainThreadMarker.store(nullptr, std::memory_order_relaxed);
            deferDestroy.store(false, std::memory_order_relaxed);

            labelCount = 0;
            subCount = 0;
        }

        bool hasAnyPending() const noexcept
        {
            for (uint16_t i = 0; i < subCount; ++i)
            {
                if (sub[i].hasItem())
                    return true;
            }
            return false;
        }

        std::optional<uint16_t> findSubIndex(Label label) const noexcept
        {
            uint16_t lo = 0;
            uint16_t hi = labelCount;

            while (lo < hi)
            {
                uint16_t mid = static_cast<uint16_t>(lo + ((hi - lo) >> 1));
                uint16_t v = labels[mid].label;

                if (v < label)
                    lo = static_cast<uint16_t>(mid + 1);
                else
                    hi = mid;
            }

            if (lo < labelCount && labels[lo].label == label)
                return labels[lo].subIndex;

            return std::nullopt;
        }
    };

    struct ProcessQueueSlot
    {
        std::atomic<uint32_t> generation{1};
        std::atomic<bool> active{false};
        ProcessQueueState state;
    };

    struct DelayedSlot
    {
        core::ThreadPool::Task task;

        ProcessQueueId qid = 0;
        uint32_t qgen = 0;
        bool hasLabel = false;
        Label label{0};

        std::atomic<bool> inUse{false};
        std::atomic<bool> completed{false};

        std::atomic<uint32_t> generation{1};
        std::atomic<uint32_t> tmTimerId{0};

        std::atomic<uint32_t> nextFree{kInvalidIndex};

        ProcessQueueRefState* owner = nullptr;
        RefTimerNode* refNode = nullptr;
    };

private:
    friend class ProcessQueue;
    friend class ProcessQueueRef;

    template <typename F>
    bool post(ProcessQueueId id, uint32_t gen, std::optional<Label> label, F&& fn) noexcept;

    template <typename F>
    bool postOwned(ProcessQueueId id,
                   uint32_t gen,
                   std::optional<Label> label,
                   ProcessQueueRefState* owner,
                   F&& fn) noexcept;

    template <typename F>
    uint32_t schedule(ProcessQueueId id,
                      uint32_t gen,
                      std::optional<Label> label,
                      ProcessQueueRefState* owner,
                      std::chrono::steady_clock::time_point expiration,
                      F&& fn) noexcept;

    void drainProcessQueue(ProcessQueueId id, uint32_t gen) noexcept;

    void destroyProcessQueue(ProcessQueueId id, uint32_t gen) noexcept;
    void finalizeDestroy(ProcessQueueId id, uint32_t gen) noexcept;

    void onTimerFired(uint32_t delayedIdx, uint32_t delayedGen) noexcept;

    uint32_t allocDelayedSlot() noexcept;
    void freeDelayedSlot(uint32_t idx) noexcept;

    void runDelayedByIndex(uint32_t idx, uint32_t expectedGen) noexcept;
    void discardFiredDelayed(uint32_t idx, uint32_t expectedGen) noexcept;

    void destroyRefState(ProcessQueueRefState* state) noexcept;

    uint32_t packDelayedHandle(uint32_t idx, uint32_t gen) const noexcept;
    uint32_t unpackDelayedIndex(uint32_t handle) const noexcept;
    uint32_t unpackDelayedGeneration(uint32_t handle) const noexcept;
    uint32_t nextDelayedGeneration(uint32_t current) const noexcept;

private:
    std::atomic<bool> stopping{false}; ///< Set during destruction to reject new posts.

    core::ThreadPool&  pool;        ///< Shared thread pool for dispatching drain tasks.
    core::TimeManager& timeManager; ///< Timer service used by @ref schedule().

    const size_t      maxProcessQueues;   ///< Maximum number of concurrent @ref ProcessQueue slots.
    ProcessQueueSlot* pqSlots = nullptr;  ///< Heap-allocated array of @ref ProcessQueueSlot records.

    std::mutex              pqAllocMtx; ///< Guards @c pqFreeIds during slot alloc/free.
    std::vector<ProcessQueueId> pqFreeIds; ///< Stack of recycled slot indices.

    const size_t  maxDelayedTimers;       ///< Maximum number of in-flight delayed tasks.
    DelayedSlot*  delayedSlots = nullptr; ///< Heap-allocated array of @ref DelayedSlot records.

    std::atomic<uint32_t> delayedFreeHead{kInvalidIndex}; ///< Head of the lock-free free-list for delayed slots.

    const uint32_t delayedIndexBits;       ///< Bit-width of the index field in a packed timer handle.
    const uint32_t delayedIndexMask;       ///< Mask isolating the index field from a packed handle.
    const uint32_t delayedGenerationMask;  ///< Mask isolating the generation field from a packed handle.

    std::atomic<uint32_t> timerInFlight{0}; ///< Count of @ref TimeManager callbacks currently pending dispatch.
};

template <typename F>
bool ControlScheduler::post(ProcessQueueId id, uint32_t gen, std::optional<Label> label, F&& fn) noexcept
{
    if (stopping.load(std::memory_order_acquire))
        return false;

    if (id >= maxProcessQueues)
        return false;

    ProcessQueueSlot& slot = pqSlots[id];

    if (!slot.active.load(std::memory_order_acquire))
        return false;

    if (slot.generation.load(std::memory_order_relaxed) != gen)
        return false;

    ProcessQueueState& st = slot.state;

    if (st.closed.load(std::memory_order_acquire))
        return false;

    uint16_t subIndex = 0;
    if (label.has_value())
    {
        auto idx = st.findSubIndex(*label);
        if (!idx.has_value())
            return false;

        subIndex = *idx;
    }

    if (!st.sub[subIndex].tryEmplace(std::forward<F>(fn)))
        return false;

    const bool was = st.scheduled.exchange(true, std::memory_order_acq_rel);
    if (!was)
    {
        while (!pool.enqueue([this, id, gen] { this->drainProcessQueue(id, gen); }))
            _mm_pause();
    }

    return true;
}

template <typename F>
bool ControlScheduler::postOwned(ProcessQueueId id,
                                 uint32_t gen,
                                 std::optional<Label> label,
                                 ProcessQueueRefState* owner,
                                 F&& fn) noexcept
{
    if (!owner)
        return false;

    if (!owner->alive.load(std::memory_order_acquire))
        return false;

    owner->pending.fetch_add(1, std::memory_order_acq_rel);

    return post(id, gen, label,
        [token = RefPendingToken(owner), fn = std::forward<F>(fn)]() mutable noexcept
        {
            fn();
        });
}

template <typename F>
uint32_t ControlScheduler::schedule(ProcessQueueId id,
                                    uint32_t gen,
                                    std::optional<Label> label,
                                    ProcessQueueRefState* owner,
                                    std::chrono::steady_clock::time_point expiration,
                                    F&& fn) noexcept
{
    if (stopping.load(std::memory_order_acquire))
        return 0;

    if (id >= maxProcessQueues)
        return 0;

    ProcessQueueSlot& slot = pqSlots[id];

    if (!slot.active.load(std::memory_order_acquire))
        return 0;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return 0;

    ProcessQueueState& st = slot.state;
    if (st.closed.load(std::memory_order_acquire))
        return 0;

    if (maxDelayedTimers == 0)
        return 0;

    if (owner && !owner->alive.load(std::memory_order_acquire))
        return 0;

    const uint32_t delayedIdx = allocDelayedSlot();
    if (delayedIdx == kInvalidIndex)
        return 0;

    DelayedSlot& ds = delayedSlots[delayedIdx];
    const uint32_t delayedGen = ds.generation.load(std::memory_order_acquire);
    const uint32_t publicHandle = packDelayedHandle(delayedIdx, delayedGen);

    ds.qid = id;
    ds.qgen = gen;
    ds.hasLabel = label.has_value();
    ds.label = label.value_or(Label{0});
    ds.completed.store(false, std::memory_order_release);
    ds.tmTimerId.store(0, std::memory_order_release);
    ds.owner = owner;
    ds.refNode = nullptr;

    if (owner)
    {
        RefTimerNode* node = new (std::nothrow) RefTimerNode();
        if (!node)
        {
            freeDelayedSlot(delayedIdx);
            return 0;
        }

        node->handle = publicHandle;
        node->active.store(true, std::memory_order_relaxed);

        RefTimerNode* head = owner->timerHead.load(std::memory_order_relaxed);
        do
        {
            node->next.store(head, std::memory_order_relaxed);
        }
        while (!owner->timerHead.compare_exchange_weak(
            head,
            node,
            std::memory_order_release,
            std::memory_order_relaxed));

        ds.refNode = node;

        owner->pending.fetch_add(1, std::memory_order_acq_rel);

        ds.task.set(
            [token = RefPendingToken(owner), fn = std::forward<F>(fn), publicHandle]() mutable noexcept
            {
                fn(publicHandle);
            });
    }
    else
    {
        ds.task.set(
            [fn = std::forward<F>(fn), publicHandle]() mutable noexcept
            {
                fn(publicHandle);
            });
    }

    const uint32_t tmId = timeManager.addTimer(
        expiration,
        [this, delayedIdx, delayedGen](uint32_t) noexcept
        {
            onTimerFired(delayedIdx, delayedGen);
        });

    if (tmId == 0)
    {
        if (ds.refNode)
            ds.refNode->active.store(false, std::memory_order_release);

        ds.task.cleanup();
        freeDelayedSlot(delayedIdx);
        return 0;
    }

    ds.tmTimerId.store(tmId, std::memory_order_release);
    return publicHandle;
}

/**
 * @brief A borrowed, lifetime-safe reference to a @ref ProcessQueue.
 * @ingroup CORE
 *
 * `ProcessQueueRef` is produced by `ProcessQueue::ref()` and may outlive the
 * originating `ProcessQueue`.  Tasks posted through a ref are silently dropped
 * if the underlying queue has been destroyed — the `alive` flag in
 * @ref ProcessQueueRefState gates execution.
 *
 * ## Lifecycle & Ownership
 * - The ref holds a heap-allocated @ref ProcessQueueRefState that is shared
 *   (by raw pointer) with every lambda it posts.
 * - `release()` (called by the destructor) sets `alive = false` and then
 *   blocks until `pending` reaches zero, ensuring no lambda executes past the
 *   ref's lifetime.
 *
 * ## Concurrency Model
 * - `post()` and `postAfter()` are safe to call from any thread.
 * - Destruction **blocks** the calling thread until all posted-but-not-yet-run
 *   lambdas have either executed or been discarded.
 *
 * @warning Never destroy a `ProcessQueueRef` from inside a task it has posted;
 * doing so causes `release()` to wait on `pending` from within the drain loop,
 * which deadlocks.
 *
 * @see ProcessQueue::ref
 */
class ProcessQueueRef
{
    friend class ControlScheduler;
    friend class ProcessQueue;

public:
    ProcessQueueRef(const ProcessQueueRef&) = delete;
    ProcessQueueRef& operator=(const ProcessQueueRef&) = delete;

    ProcessQueueRef(ProcessQueueRef&& o) noexcept
        : engine(o.engine),
          id(o.id),
          gen(o.gen),
          state(o.state)
    {
        o.engine = nullptr;
        o.id = 0;
        o.gen = 0;
        o.state = nullptr;
    }

    ProcessQueueRef& operator=(ProcessQueueRef&& o) noexcept
    {
        if (this == &o)
            return *this;

        release();

        engine = o.engine;
        id = o.id;
        gen = o.gen;
        state = o.state;

        o.engine = nullptr;
        o.id = 0;
        o.gen = 0;
        o.state = nullptr;

        return *this;
    }

    ~ProcessQueueRef()
    {
        release();
    }

    /**
     * @brief Releases this ref early, blocking until in-flight tasks finish.
     *
     * Equivalent to what the destructor does, but callable explicitly so an
     * owner can guarantee no posted task is running before tearing down the
     * members that task would touch. Safe to call multiple times; the ref is
     * left empty (default-constructed) afterward.
     */
    void release() noexcept
    {
        if (!state)
        {
            engine = nullptr;
            id = 0;
            gen = 0;
            return;
        }

        state->alive.store(false, std::memory_order_release);

        if (engine)
            engine->destroyRefState(state);

        uint32_t v = state->pending.load(std::memory_order_acquire);
        while (v != 0)
        {
            state->pending.wait(v, std::memory_order_relaxed);
            v = state->pending.load(std::memory_order_acquire);
        }

        delete state;
        state = nullptr;
        engine = nullptr;
        id = 0;
        gen = 0;
    }

    /**
     * @brief Posts a task to the default sub-queue, guarded by the ref's lifetime.
     *
     * The task is silently dropped (not executed) if the @ref ProcessQueueRefState
     * `alive` flag is `false` at execution time.
     *
     * @tparam F  Callable type; must fit in the inline task storage (≤128 bytes).
     * @param fn  Task to execute on the queue's drain thread.
     * @return `true` if the task was enqueued; `false` if the queue is closed or full.
     */
    template <typename F>
    bool post(F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, std::nullopt, state, std::forward<F>(fn));
    }

    /**
     * @brief Posts a task to a specific labeled sub-queue, guarded by the ref's lifetime.
     *
     * @tparam F      Callable type.
     * @param label   Sub-queue label registered at @ref ProcessQueue creation time.
     * @param fn      Task to execute.
     * @return `true` if enqueued; `false` if the label is unknown, queue closed, or full.
     */
    template <typename F>
    bool post(ControlScheduler::Label label, F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, label, state, std::forward<F>(fn));
    }

    /**
     * @brief Posts a task to the default sub-queue and blocks the calling
     *        thread until that exact task has finished executing.
     *
     * Use this when a teardown step must be guaranteed complete before the
     * calling thread proceeds to destroy state the task might touch (e.g.
     * `InterfaceManager::deactivateAll()` erasing entries that a concurrently
     * running `refreshInterfaceList()` is iterating).
     *
     * @return `true` if the task was enqueued and ran (or was dropped because
     *         the ref is no longer alive); `false` if it could not be posted
     *         at all (queue closed/full), in which case `fn` did not run.
     *
     * @warning Do not call this from within a task already running on this
     * ref's queue: the queue is single-consumer, so the calling thread would
     * block forever waiting for a task that can only run after it returns.
     */
    template <typename F>
    bool postAndWait(F&& fn) const
    {
        if (!engine || !state)
            return false;

        std::promise<void> done;
        std::future<void> fut = done.get_future();

        bool posted = engine->postOwned(id, gen, std::nullopt, state,
            [fn = std::forward<F>(fn), &done]() mutable {
                fn();
                done.set_value();
            });

        if (!posted)
            return false;

        fut.wait();
        return true;
    }

    /**
     * @brief Schedules a task to run after `expiration`, guarded by the ref's lifetime.
     *
     * The callback receives the timer handle as its only argument so it can
     * distinguish which timer fired if multiple are outstanding.
     *
     * @tparam F          Callable of type `void(uint32_t)`.
     * @param expiration  Absolute time at which the task should fire.
     * @param fn          Task; receives its own timer handle as argument.
     * @return Non-zero timer handle on success; `0` on failure (scheduler stopping, no slot).
     */
    template <typename F>
    uint32_t postAfter(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, std::nullopt, state, expiration, std::forward<F>(fn));
    }

    /**
     * @brief Schedules a task to a labeled sub-queue after `expiration`.
     *
     * @tparam F          Callable of type `void(uint32_t)`.
     * @param label       Sub-queue label.
     * @param expiration  Absolute expiry time point.
     * @param fn          Task; receives its timer handle as argument.
     * @return Non-zero timer handle on success; `0` on failure.
     */
    template <typename F>
    uint32_t postAfter(ControlScheduler::Label label,
                       std::chrono::steady_clock::time_point expiration,
                       F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, label, state, expiration, std::forward<F>(fn));
    }

    /**
     * @brief Cancels a pending delayed task by its handle.
     *
     * @param timerId  Handle returned by @ref postAfter.
     * @return `true` if the timer was cancelled before firing.
     */
    bool cancel(uint32_t timerId) noexcept
    {
        if (!engine)
            return false;

        return engine->cancelDelayed(timerId);
    }

    ControlScheduler::ProcessQueueId getId() const noexcept { return id; }
    uint32_t generation() const noexcept { return gen; }

private:
    ProcessQueueRef() = default;

    ProcessQueueRef(ControlScheduler& e,
                    ControlScheduler::ProcessQueueId qid,
                    uint32_t qgen) noexcept
        : engine(&e),
          id(qid),
          gen(qgen),
          state(new ProcessQueueRefState())
    {}

private:
    ControlScheduler* engine = nullptr;            ///< Owning scheduler; null for a moved-from ref.
    ControlScheduler::ProcessQueueId id = 0;       ///< Slot index of the target @ref ProcessQueue.
    uint32_t gen = 0;                              ///< Generation counter; guards against ABA reuse.
    ProcessQueueRefState* state = nullptr;         ///< Shared lifetime state; heap-allocated.
};

/**
 * @brief Owning handle for a single serialized execution context within @ref ControlScheduler.
 * @ingroup CORE
 *
 * A `ProcessQueue` is a single-consumer task queue: the @ref ControlScheduler
 * guarantees that only one thread executes its tasks at any moment, making it safe
 * to use as a synchronization boundary for protocol state machines without internal
 * mutexes.
 *
 * Supports up to `kMaxSubQueues` labeled sub-queues so that callers can separate
 * work classes (e.g. high-priority hellos vs. low-priority route updates) within the
 * same serialized context.
 *
 * ## Lifecycle & Ownership
 * - Created exclusively by `ControlScheduler::create()`.
 * - Move-only: transferring ownership transfers the underlying slot.
 * - Calling `reset()` (or letting the object go out of scope) destroys the slot and
 *   waits for the drain task to finish if one is currently executing.
 *
 * ## Concurrency Model
 * - `post()` and `schedule()` are safe to call from any thread.
 * - The drain function serializes all tasks; callers must not assume which thread runs them.
 *
 * @warning Do not call `reset()` from within a task posted to this queue;
 * `destroyProcessQueue` will attempt to drain the queue, resulting in a deadlock.
 *
 * @see ProcessQueueRef
 * @see ControlScheduler::create
 */
class ProcessQueue
{
    friend class ControlScheduler;

public:
    ProcessQueue() = default;

    /** @brief Destroys the queue, releasing its slot back to the scheduler. */
    ~ProcessQueue() { reset(); }

    ProcessQueue(const ProcessQueue&) = delete;
    ProcessQueue& operator=(const ProcessQueue&) = delete;

    ProcessQueue(ProcessQueue&& o) noexcept
        : engine(o.engine),
          id(o.id),
          gen(o.gen)
    {
        o.engine = nullptr;
        o.id = 0;
        o.gen = 0;
    }

    ProcessQueue& operator=(ProcessQueue&& o) noexcept
    {
        if (this == &o)
            return *this;

        reset();

        engine = o.engine;
        id = o.id;
        gen = o.gen;

        o.engine = nullptr;
        o.id = 0;
        o.gen = 0;
        return *this;
    }

    /**
     * @brief Creates a @ref ProcessQueueRef that borrows this queue with safe lifetime semantics.
     *
     * The returned ref may be stored in objects whose lifetime is shorter than the queue's.
     * Tasks posted through the ref after the ref is destroyed are silently discarded.
     *
     * @note This is the only way to enqueue work on a @ref ProcessQueue. Posting and
     * scheduling are deliberately not exposed directly on `ProcessQueue` — a
     * `ProcessQueueRef` ties posted tasks to a lifetime that can be safely waited on
     * and released before the owning object's members are torn down.
     */
    ProcessQueueRef ref() const noexcept
    {
        if (!engine)
            return ProcessQueueRef();

        return ProcessQueueRef(*engine, id, gen);
    }

    /**
     * @brief Blocks until this queue has drained all pending and in-flight tasks.
     *
     * Intended for tests that post asynchronous work (e.g. via a config-change
     * applier) and need to wait for it to complete before asserting on state.
     *
     * @warning Do not call from within a task posted to this queue; that
     * deadlocks because the queue cannot finish draining while the calling
     * task is still running.
     */
    void waitIdle() const noexcept
    {
        if (engine)
            engine->waitIdle(id, gen);
    }

    /**
     * @brief Destroys the queue and releases its slot back to the scheduler.
     *
     * Safe to call multiple times. Waits for any currently-executing drain to finish
     * before returning.  All pending but unstarted tasks are discarded.
     */
    void reset() noexcept
    {
        if (engine)
            engine->destroyProcessQueue(id, gen);

        engine = nullptr;
        id = 0;
        gen = 0;
    }

    ControlScheduler::ProcessQueueId getId() const noexcept { return id; }
    uint32_t generation() const noexcept { return gen; }

private:
    ProcessQueue(ControlScheduler& e, ControlScheduler::ProcessQueueId qid, uint32_t qgen) noexcept
        : engine(&e),
          id(qid),
          gen(qgen)
    {}

private:
    ControlScheduler* engine = nullptr;      ///< Owning scheduler; null for a default-constructed or moved-from queue.
    ControlScheduler::ProcessQueueId id = 0; ///< Slot index within @c ControlScheduler::pqSlots.
    uint32_t gen = 0;                        ///< Generation counter matching @c ProcessQueueSlot::generation.
};

} // namespace core

#endif // CONTROL_ENGINE_H

