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
#include <new>
#include <immintrin.h>

#include <ThreadPool.hpp>
#include <TimeManager.h>

namespace core
{

class ProcessQueueRef;
class ProcessQueue;

/**
 * @brief Node tracking an in-flight delayed timer owned by a @ref ProcessQueueRef.
 * @ingroup CORE
 *
 * List structure is guarded by @ref ProcessQueueRefState::listLock; `active`
 * is atomic so the fire/cancel paths can mark a node without the lock.
 * Inactive nodes are pruned on the next postAfter() or on ref release, so the
 * list stays bounded for long-lived refs.
 */
struct RefTimerNode
{
    RefTimerNode* next = nullptr;   ///< Next node; guarded by the owner's listLock.
    std::atomic<bool> active{true}; ///< False once the timer has fired or been cancelled.
    uint32_t handle = 0;            ///< Public timer handle returned to the caller.
};

/**
 * @brief Shared lifetime-tracking state for a @ref ProcessQueueRef.
 * @ingroup CORE
 *
 * `alive` gates execution of posted callbacks after release; `pending` counts
 * in-flight tasks — release() blocks until it reaches zero.
 */
struct ProcessQueueRefState
{
    std::atomic<bool> alive{true};
    std::atomic<uint32_t> pending{0};

    std::atomic_flag listLock = ATOMIC_FLAG_INIT; ///< Spinlock guarding the timer-node list.
    RefTimerNode* timerHead = nullptr;            ///< Timer-node list (guarded by listLock).
};

/** @brief Decrements `state->pending` and wakes release() when it hits zero. */
inline void releaseRefPending(ProcessQueueRefState* state) noexcept
{
    if (!state)
        return;

    const uint32_t prev = state->pending.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
        state->pending.notify_all();
}

/**
 * @brief RAII token captured by every ref-posted lambda; decrements the
 * owner's `pending` when the lambda completes or is discarded.
 * @ingroup CORE
 */
struct RefPendingToken
{
    ProcessQueueRefState* state = nullptr;

    RefPendingToken() = default;

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
 * @brief Serialized task scheduler multiplexing control-plane work over a shared @ref ThreadPool.
 * @ingroup CORE
 *
 * Provides a pool of individually-serialized execution contexts
 * (@ref ProcessQueue): only one thread runs a queue's tasks at a time, so
 * protocol state machines need no internal locking. Each queue has up to 8
 * labeled sub-queues for priority separation, and delayed tasks are backed by
 * a @ref TimeManager timer plus a fixed pool of @ref DelayedSlot entries.
 *
 * **Destruction order contract**: the @ref ThreadPool must outlive the
 * @ref TimeManager, which must outlive this scheduler, which must outlive all
 * `ProcessQueue`/`ProcessQueueRef` objects created from it. External posters
 * must stop before a queue is destroyed (ref-based posting enforces this via
 * the `pending` protocol).
 *
 * @see ProcessQueue
 * @see ProcessQueueRef
 */
class ControlScheduler
{
    static constexpr uint64_t kMaxSubQueues = 8;
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    struct ProcessQueueSlot;

public:
    using ProcessQueueId = ProcessQueueSlot*; ///< Opaque handle identifying a @ref ProcessQueue's slot.
    using Label = uint16_t;                   ///< Sub-queue selector for priority/class separation.

    /** @brief Configuration for one labeled sub-queue. */
    struct SubQueueConfig
    {
        Label label;       ///< Unique label identifying this sub-queue.
        uint32_t capacity; ///< Ring capacity (must be a power of two).
    };

    /**
     * @param externalPool     ThreadPool used to dispatch drain tasks.
     * @param tmgr             TimeManager used for delayed-task timers.
     * @param reserveQueues    Number of @ref ProcessQueue slots to pre-allocate; the pool
     *                         grows without limit beyond this as needed.
     * @param maxDelayedTimers Maximum in-flight delayed tasks.
     */
    explicit ControlScheduler(core::ThreadPool& externalPool,
                              core::TimeManager& tmgr,
                              size_t reserveQueues = 0,
                              size_t maxDelayedTimers = 4096);

    /** @brief Tears down all remaining queues, then frees the slot nodes. */
    ~ControlScheduler();

    ControlScheduler(const ControlScheduler&) = delete;
    ControlScheduler& operator=(const ControlScheduler&) = delete;

    /** @brief Returns the @ref TimeManager used for delayed task scheduling. */
    core::TimeManager& timers() noexcept { return timeManager; }

    /**
     * @brief Allocates a new serialized @ref ProcessQueue.
     *
     * @param capacity  Default sub-queue ring capacity (power of two).
     * @param labeled   Optional labeled sub-queues (unique labels required).
     */
    ProcessQueue create(uint32_t capacity = 4096,
                        std::initializer_list<SubQueueConfig> labeled = {});

    /**
     * @brief Cancels a pending delayed task by its handle; safe from any thread.
     * @return true if cancelled before firing; false if fired or stale.
     */
    bool cancelDelayed(uint32_t timerId) noexcept;

private:
    /**
     * @brief Blocks until the queue has no in-flight or pending tasks.
     * @warning Deadlocks if called from a task running on this queue.
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

        /// Threads currently inside post()/schedule() for this slot.
        /// finalizeDestroy() waits for zero before freeing the rings, so a
        /// racing poster can never write into freed memory. Never reset
        /// (transient stale posts balance it).
        std::atomic<uint32_t> posters{0};

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
            for (uint16_t i = 0; i < labelCount; ++i)
            {
                if (labels[i].label == label)
                    return labels[i].subIndex;
            }
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

        ProcessQueueId qid = nullptr;
        uint32_t qgen = 0;
        bool hasLabel = false;
        Label label{0};

        std::atomic<bool> inUse{false};
        std::atomic<bool> completed{false};

        std::atomic<uint32_t> generation{1};
        std::atomic<uint32_t> tmTimerId{0};

        std::atomic<uint32_t> nextFree{kInvalidIndex};

        RefTimerNode* refNode = nullptr;
    };

    /**
     * @brief RAII increment of @ref ProcessQueueState::posters.
     *
     * The seq_cst increment orders against finalizeDestroy()'s seq_cst
     * generation bump: a poster either sees the new generation (and aborts)
     * or is visible to the finalizer's posters-drain wait.
     */
    struct PosterGuard
    {
        std::atomic<uint32_t>& counter;

        explicit PosterGuard(std::atomic<uint32_t>& c) noexcept
            : counter(c)
        {
            counter.fetch_add(1, std::memory_order_seq_cst);
        }

        ~PosterGuard() noexcept
        {
            counter.fetch_sub(1, std::memory_order_release);
        }

        PosterGuard(const PosterGuard&) = delete;
        PosterGuard& operator=(const PosterGuard&) = delete;
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

    /// Runs (or discards) a fired delayed task and frees its slot.
    void finishFiredDelayed(uint32_t idx, uint32_t expectedGen, bool run) noexcept;

    bool linkRefTimerNode(ProcessQueueRefState& owner, RefTimerNode* node) noexcept;
    void destroyRefState(ProcessQueueRefState* state) noexcept;

    uint32_t packDelayedHandle(uint32_t idx, uint32_t gen) const noexcept;
    uint32_t unpackDelayedIndex(uint32_t handle) const noexcept;
    uint32_t unpackDelayedGeneration(uint32_t handle) const noexcept;
    uint32_t nextDelayedGeneration(uint32_t current) const noexcept;

private:
    std::atomic<bool> stopping{false}; ///< Set during destruction to reject new posts.

    core::ThreadPool&  pool;
    core::TimeManager& timeManager;

    std::mutex                   pqAllocMtx; ///< Guards pqAllNodes/pqFreeNodes.
    std::vector<ProcessQueueSlot*> pqAllNodes;  ///< Every slot node ever allocated; walked on destruction.
    std::vector<ProcessQueueSlot*> pqFreeNodes; ///< Recycled, idle slot nodes ready for reuse by create().

    const size_t  maxDelayedTimers;
    DelayedSlot*  delayedSlots = nullptr;

    std::atomic<uint32_t> delayedFreeHead{kInvalidIndex}; ///< Lock-free free-list of delayed slots.

    const uint32_t delayedIndexBits;      ///< Index-field width in a packed timer handle.
    const uint32_t delayedIndexMask;
    const uint32_t delayedGenerationMask;

    std::atomic<uint32_t> timerInFlight{0};  ///< onTimerFired() callbacks currently executing.
    std::atomic<uint32_t> drainsInFlight{0}; ///< drainProcessQueue() invocations currently executing.
};

template <typename F>
bool ControlScheduler::post(ProcessQueueId id, uint32_t gen, std::optional<Label> label, F&& fn) noexcept
{
    if (stopping.load(std::memory_order_acquire))
        return false;

    if (!id)
        return false;

    ProcessQueueSlot& slot = *id;
    ProcessQueueState& st = slot.state;

    // Hold the poster count across all slot-state access so finalizeDestroy()
    // cannot free the sub-queue rings underneath us. Checks happen after the
    // increment; see PosterGuard for the ordering argument.
    PosterGuard poster(st.posters);

    if (!slot.active.load(std::memory_order_acquire))
        return false;

    if (slot.generation.load(std::memory_order_seq_cst) != gen)
        return false;

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

    if (!id)
        return 0;

    ProcessQueueSlot& slot = *id;
    ProcessQueueState& st = slot.state;

    PosterGuard poster(st.posters);

    if (!slot.active.load(std::memory_order_acquire))
        return 0;

    if (slot.generation.load(std::memory_order_seq_cst) != gen)
        return 0;

    if (st.closed.load(std::memory_order_acquire))
        return 0;

    if (label.has_value() && !st.findSubIndex(*label).has_value())
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

        // Link under the list lock. Re-checking `alive` inside the lock makes
        // release() airtight: destroyRefState() detaches the list under the
        // same lock after `alive` is already false, so either we abort here or
        // the node is in the list when it is detached and gets cancelled.
        if (!linkRefTimerNode(*owner, node))
        {
            delete node;
            freeDelayedSlot(delayedIdx);
            return 0;
        }

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

    // Final publish step. The fire/cancel paths wait for a non-zero id before
    // freeing the slot, so this store can never land in a recycled slot even
    // if the timer fires immediately.
    const uint32_t tmId = timeManager.addTimer(
        expiration,
        [this, delayedIdx, delayedGen](uint32_t) noexcept
        {
            onTimerFired(delayedIdx, delayedGen);
        });

    ds.tmTimerId.store(tmId, std::memory_order_release);
    return publicHandle;
}

/**
 * @brief A borrowed, lifetime-safe reference to a @ref ProcessQueue.
 * @ingroup CORE
 *
 * May outlive the originating queue; tasks posted through a released or stale
 * ref are silently dropped. release() (also run by the destructor) blocks
 * until all posted-but-unfinished tasks have executed or been discarded, so
 * no lambda runs past the ref's lifetime.
 *
 * @warning Never release/destroy a ref from inside a task it posted — the
 * wait on `pending` would deadlock.
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
        o.id = nullptr;
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
        o.id = nullptr;
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
     * Safe to call multiple times; the ref is left empty afterward.
     */
    void release() noexcept
    {
        if (!state)
        {
            engine = nullptr;
            id = nullptr;
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
        id = nullptr;
        gen = 0;
    }

    /**
     * @brief Posts a task to the default sub-queue, guarded by the ref's lifetime.
     * @return true if enqueued; false if the queue is closed or full.
     */
    template <typename F>
    bool post(F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, std::nullopt, state, std::forward<F>(fn));
    }

    /** @brief Posts a task to a labeled sub-queue, guarded by the ref's lifetime. */
    template <typename F>
    bool post(ControlScheduler::Label label, F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, label, state, std::forward<F>(fn));
    }

    /**
     * @brief Posts a task and blocks until that exact task has finished
     * executing (or been discarded).
     *
     * @return true if the task was enqueued and ran or was dropped; false if
     *         it could not be posted at all, in which case `fn` did not run.
     * @warning Deadlocks if called from a task already running on this queue.
     */
    template <typename F>
    bool postAndWait(F&& fn) const
    {
        if (!engine || !state)
            return false;

        std::promise<void> done;
        std::future<void> fut = done.get_future();

        // The waiter must be released even when the task is *discarded*
        // (queue closed between post and drain destroys the lambda without
        // running it), so the promise is satisfied from the destructor.
        struct Notifier
        {
            std::promise<void>* p;
            bool fired = false;

            explicit Notifier(std::promise<void>* pr) noexcept : p(pr) {}

            Notifier(Notifier&& o) noexcept : p(o.p), fired(o.fired) { o.p = nullptr; }
            Notifier(const Notifier&) = delete;

            void fire()
            {
                if (p && !fired)
                {
                    p->set_value();
                    fired = true;
                }
            }

            ~Notifier() { fire(); }
        };

        bool posted = engine->postOwned(id, gen, std::nullopt, state,
            [fn = std::forward<F>(fn), n = Notifier(&done)]() mutable {
                fn();
                n.fire();
            });

        if (!posted)
            return false;

        fut.wait();
        return true;
    }

    /**
     * @brief Schedules a task to run at `expiration`, guarded by the ref's
     * lifetime. The callback receives its own timer handle.
     * @return Non-zero timer handle, or 0 on failure.
     */
    template <typename F>
    uint32_t postAfter(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, std::nullopt, state, expiration, std::forward<F>(fn));
    }

    /** @brief Labeled-sub-queue variant of @ref postAfter. */
    template <typename F>
    uint32_t postAfter(ControlScheduler::Label label,
                       std::chrono::steady_clock::time_point expiration,
                       F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, label, state, expiration, std::forward<F>(fn));
    }

    /** @brief Cancels a pending delayed task; true if cancelled before firing. */
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
    ControlScheduler* engine = nullptr;      ///< Null for a moved-from ref.
    ControlScheduler::ProcessQueueId id = nullptr;
    uint32_t gen = 0;                        ///< Guards against ABA slot reuse.
    ProcessQueueRefState* state = nullptr;   ///< Shared lifetime state; heap-allocated.
};

/**
 * @brief Owning handle for one serialized execution context in @ref ControlScheduler.
 * @ingroup CORE
 *
 * Move-only. All posting goes through @ref ref(), which ties tasks to a
 * releasable lifetime. reset() (also run by the destructor) discards pending
 * tasks and waits for a running drain before recycling the slot.
 *
 * @warning Do not call reset() or waitIdle() from a task posted to this
 * queue; both deadlock (reset defers safely, but waitIdle cannot).
 *
 * @see ProcessQueueRef
 */
class ProcessQueue
{
    friend class ControlScheduler;

public:
    ProcessQueue() = default;

    ~ProcessQueue() { reset(); }

    ProcessQueue(const ProcessQueue&) = delete;
    ProcessQueue& operator=(const ProcessQueue&) = delete;

    ProcessQueue(ProcessQueue&& o) noexcept
        : engine(o.engine),
          id(o.id),
          gen(o.gen)
    {
        o.engine = nullptr;
        o.id = nullptr;
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
        o.id = nullptr;
        o.gen = 0;
        return *this;
    }

    /**
     * @brief Creates a @ref ProcessQueueRef borrowing this queue — the only
     * way to enqueue work, so every task is tied to a releasable lifetime.
     */
    ProcessQueueRef ref() const noexcept
    {
        if (!engine)
            return ProcessQueueRef();

        return ProcessQueueRef(*engine, id, gen);
    }

    /** @brief Blocks until all pending and in-flight tasks have completed. */
    void waitIdle() const noexcept
    {
        if (engine)
            engine->waitIdle(id, gen);
    }

    /**
     * @brief Destroys the queue and recycles its slot. Safe to call multiple
     * times; pending but unstarted tasks are discarded.
     */
    void reset() noexcept
    {
        if (engine)
            engine->destroyProcessQueue(id, gen);

        engine = nullptr;
        id = nullptr;
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
    ControlScheduler* engine = nullptr;      ///< Null for a default-constructed or moved-from queue.
    ControlScheduler::ProcessQueueId id = nullptr;
    uint32_t gen = 0;                        ///< Must match the slot's generation.
};

} // namespace core

#endif // CONTROL_ENGINE_H
