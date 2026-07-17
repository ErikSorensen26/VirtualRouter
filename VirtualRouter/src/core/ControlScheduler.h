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
#include <mutex>
#include <utility>
#include <vector>
#include <new>
#include <immintrin.h>

#include <ThreadPool.hpp>
#include <TimeManager.h>
#include <Mock.hpp>

namespace core
{

class ProcessQueue;

/**
 * @brief Serialized task scheduler multiplexing control-plane work over a shared @ref ThreadPool.
 * @ingroup CORE
 *
 * Provides a pool of individually-serialized execution contexts
 * (@ref ProcessQueue): only one thread runs a queue's tasks at a time, so
 * protocol state machines need no internal locking. Delayed tasks are backed
 * by a @ref TimeManager timer plus a fixed pool of @ref DelayedSlot entries.
 *
 * ## Concurrency invariants
 * - At most one drain runs per queue at any moment; posters hand off via a
 *   single Idle/Scheduled/Running/Rescheduled state machine, so a drain can
 *   never overlap another drain or lose a wakeup.
 * - Every thread that touches a queue's ring memory (post, drain, waitIdle)
 *   holds the slot's `accessors` guard; destruction claims the slot's
 *   generation atomically and waits for `accessors == 0` before freeing, so
 *   ring memory is never reclaimed under a reader.
 *
 * **Destruction order contract**: the @ref ThreadPool must outlive the
 * @ref TimeManager, which must outlive this scheduler, which must outlive all
 * @ref ProcessQueue handles created from it.
 *
 * @see ProcessQueue
 */
class ControlScheduler
{
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    struct ProcessQueueSlot;

public:
    using ProcessQueueId = ProcessQueueSlot*; ///< Opaque handle identifying a @ref ProcessQueue's slot.

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
     * @brief Allocates a new serialized @ref ProcessQueue (owning handle).
     *
     * @param capacity  Task ring capacity (power of two).
     */
    ProcessQueue create(uint32_t capacity = 4096);

    /**
     * @brief Cancels a pending delayed task by its handle; safe from any thread.
     * @return true if cancelled before firing; false if fired or stale.
     */
    bool cancelDelayed(uint32_t timerId) noexcept;

private:
    // ---------------------------------------------------------------------
    // Per-handle lifetime state
    // ---------------------------------------------------------------------

    /**
     * @brief Node tracking an in-flight delayed timer owned by a handle.
     *
     * Jointly owned by the handle's timer list and by the delayed slot's
     * fire/cancel path (`owners` starts at 2); the last owner deletes it, so
     * neither side ever touches a freed node and no global synchronization
     * is needed between a firing timer and a releasing handle.
     */
    struct RefTimerNode
    {
        RefTimerNode* next = nullptr;    ///< Next node; guarded by the owner's listLock.
        std::atomic<bool> active{true};  ///< False once the timer has fired or been cancelled.
        std::atomic<uint32_t> owners{2}; ///< List-side + timer-side ownership count.
        uint32_t handle = 0;             ///< Public timer handle returned to the caller.
    };

    /**
     * @brief Shared lifetime state for one @ref ProcessQueue handle.
     *
     * `alive` gates new posts after release; `pending` counts in-flight tasks.
     * release() blocks (or pumps, on the drain thread) until only the caller's
     * own task remains; if released from inside one of its own tasks the state
     * is detached and the final task token deletes it.
     */
    struct RefState
    {
        std::atomic<bool> alive{true};
        std::atomic<uint32_t> pending{0};
        std::atomic<bool> detached{false}; ///< Set by release() from own task; last token deletes.

        std::atomic_flag listLock = ATOMIC_FLAG_INIT; ///< Spinlock guarding the timer-node list.
        RefTimerNode* timerHead = nullptr;            ///< Timer-node list (guarded by listLock).
    };

    /** @brief Drops one ownership share of a timer node; last owner deletes. */
    static void dropNodeOwner(RefTimerNode* node) noexcept
    {
        if (node->owners.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete node;
    }

    /** @brief Task-token decrement of `pending`; wakes release() or, when the
     * state was detached by a release() from inside this very task, deletes it. */
    static void releasePending(RefState* state) noexcept
    {
        if (!state)
            return;

        // `detached` can only be true if release() ran earlier on this same
        // thread (inside the task this token belongs to), so a plain pre-read
        // is race-free.
        const bool detached = state->detached.load(std::memory_order_acquire);
        const uint32_t prev = state->pending.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1)
        {
            if (detached)
                delete state;
            else
                state->pending.notify_all();
        }
    }

    /**
     * @brief RAII token captured by every owned task; decrements the owner's
     * `pending` when the task completes or is discarded.
     */
    struct PendingToken
    {
        RefState* state = nullptr;

        PendingToken() = default;

        explicit PendingToken(RefState* s) noexcept : state(s) {}

        PendingToken(const PendingToken&) = delete;
        PendingToken& operator=(const PendingToken&) = delete;

        PendingToken(PendingToken&& o) noexcept : state(o.state) { o.state = nullptr; }
        PendingToken& operator=(PendingToken&&) = delete;

        ~PendingToken() noexcept
        {
            if (state)
                releasePending(state);
        }
    };

    static inline thread_local RefState* tlsCurrentTaskOwner = nullptr;

    // RING QUEUES

    struct TaskRing
    {
        struct Slot
        {
            std::atomic<uint64_t> seq;
            core::ThreadPool::Task task;
        };

        TaskRing() = default;
        ~TaskRing() { reset(); }

        TaskRing(const TaskRing&) = delete;
        TaskRing& operator=(const TaskRing&) = delete;

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

    private:
        uint32_t capacity = 0;
        uint32_t mask = 0;
        Slot* slots = nullptr;

        alignas(64) std::atomic<uint64_t> head{0};
        alignas(64) std::atomic<uint64_t> tail{0};

        INJECT_MOCK(MOCK_PROCESS_QUEUE_MUTEX) ///< Mock injection for lock during testing
    };

    /// Drain-ownership state machine; at most one drain runs per queue.
    enum RunState : uint32_t
    {
        RS_IDLE = 0,      ///< No drain scheduled or running.
        RS_SCHEDULED = 1, ///< A drain task is enqueued on the pool.
        RS_RUNNING = 2,   ///< The (single) drain is consuming tasks.
        RS_RESCHED = 3,   ///< Items arrived while running; drain must loop again.
    };

    struct ProcessQueueSlot
    {
        std::atomic<uint32_t> generation{1}; ///< Claimed (CAS +1) by finalizeDestroy.
        std::atomic<bool> active{false};     ///< True once create() finished configuring.

        std::atomic<uint32_t> accessors{0};

        std::atomic<uint32_t> runState{RS_IDLE};
        std::atomic<bool> closed{false};
        std::atomic<bool> deferDestroy{false};
        std::atomic<void*> drainThreadMarker{nullptr};

        std::mutex waitMtx;
        std::condition_variable waitCv;

        TaskRing sub;

        void resetConfig() noexcept
        {
            closed.store(false, std::memory_order_relaxed);
            runState.store(RS_IDLE, std::memory_order_relaxed);
            deferDestroy.store(false, std::memory_order_relaxed);
            drainThreadMarker.store(nullptr, std::memory_order_relaxed);
        }
    };

    /**
     * @brief RAII increment of @ref ProcessQueueSlot::accessors.
     *
     * The seq_cst increment orders against finalizeDestroy()'s seq_cst
     * generation claim: an accessor either sees the bumped generation (and
     * aborts) or is visible to the finalizer's accessors-drain wait, so slot
     * memory is never freed under it.
     */
    struct AccessGuard
    {
        std::atomic<uint32_t>& counter;

        explicit AccessGuard(std::atomic<uint32_t>& c) noexcept
            : counter(c)
        {
            counter.fetch_add(1, std::memory_order_seq_cst);
        }

        ~AccessGuard() noexcept
        {
            counter.fetch_sub(1, std::memory_order_release);
        }

        AccessGuard(const AccessGuard&) = delete;
        AccessGuard& operator=(const AccessGuard&) = delete;
    };

    struct DelayedSlot
    {
        core::ThreadPool::Task task;

        ProcessQueueId qid = nullptr;
        uint32_t qgen = 0;

        std::atomic<bool> inUse{false};

        std::atomic<uint64_t> genClaim{1u << 1};

        std::atomic<int64_t> expiration{0}; ///< Due time (steady_clock ticks); read by waitScheduled.

        std::atomic<uint32_t> tmTimerId{0};

        std::atomic<uint32_t> nextFree{kInvalidIndex};

        RefTimerNode* refNode = nullptr;
    };

private:
    friend class ProcessQueue;

    template <typename F>
    bool post(ProcessQueueId id, uint32_t gen, F&& fn) noexcept;

    template <typename F>
    bool postOwned(ProcessQueueId id, uint32_t gen, RefState* owner, F&& fn) noexcept;

    template <typename F>
    uint32_t schedule(ProcessQueueId id,
                      uint32_t gen,
                      RefState* owner,
                      std::chrono::steady_clock::time_point expiration,
                      F&& fn) noexcept;

    void drainProcessQueue(ProcessQueueId id, uint32_t gen) noexcept;

    static bool consumeOne(ProcessQueueSlot& slot) noexcept;

    bool onDrainThread(ProcessQueueId id) const noexcept;

    bool pumpOwnDrainThread(ProcessQueueId id) noexcept;

    void waitIdle(ProcessQueueId id, uint32_t gen) noexcept;

    void waitScheduled(ProcessQueueId id, uint32_t gen) noexcept;

    void destroyProcessQueue(ProcessQueueId id, uint32_t gen) noexcept;
    void finalizeDestroy(ProcessQueueId id, uint32_t gen) noexcept;

    void onTimerFired(uint32_t delayedIdx, uint32_t delayedGen) noexcept;

    uint32_t allocDelayedSlot() noexcept;
    void freeDelayedSlot(uint32_t idx) noexcept;

    void finishFiredDelayed(uint32_t idx, uint32_t expectedGen, bool run) noexcept;

    void retireFiredDelayed(uint32_t idx, uint32_t expectedGen) noexcept;

    bool linkRefTimerNode(RefState& owner, RefTimerNode* node) noexcept;

    void cancelRefTimers(RefState* state) noexcept;

    uint32_t packDelayedHandle(uint32_t idx, uint32_t gen) const noexcept;
    uint32_t unpackDelayedIndex(uint32_t handle) const noexcept;
    uint32_t unpackDelayedGeneration(uint32_t handle) const noexcept;
    uint32_t nextDelayedGeneration(uint32_t current) const noexcept;

private:
    std::atomic<bool> stopping{false}; ///< Set during destruction to reject new posts.

    core::ThreadPool&  pool;
    core::TimeManager& timeManager;

    std::mutex                     pqAllocMtx;  ///< Guards pqAllNodes/pqFreeNodes.
    std::vector<ProcessQueueSlot*> pqAllNodes;  ///< Every slot node ever allocated; walked on destruction.
    std::vector<ProcessQueueSlot*> pqFreeNodes; ///< Recycled, idle slot nodes ready for reuse by create().

    const size_t  maxDelayedTimers;
    DelayedSlot*  delayedSlots = nullptr;

    /**
     * Lock-free LIFO free list of delayed slots. The low 32 bits hold the
     * head index, the high 32 bits an ABA tag bumped on every push/pop.
     */
    std::atomic<uint64_t> delayedFreeHead;

    const uint32_t delayedIndexBits; ///< Index-field width in a packed timer handle.
    const uint32_t delayedIndexMask;
    const uint32_t delayedGenerationMask;

    std::atomic<uint32_t> drainsInFlight{0}; ///< Drain tasks enqueued on the pool or currently executing.
};

template <typename F>
bool ControlScheduler::post(ProcessQueueId id, uint32_t gen, F&& fn) noexcept
{
    if (stopping.load(std::memory_order_acquire))
        return false;

    if (!id)
        return false;

    ProcessQueueSlot& slot = *id;

    if (!slot.active.load(std::memory_order_acquire) ||
        slot.generation.load(std::memory_order_acquire) != gen)
        return false;

    AccessGuard guard(slot.accessors);

    if (!slot.active.load(std::memory_order_seq_cst))
        return false;

    if (slot.generation.load(std::memory_order_seq_cst) != gen)
        return false;

    if (slot.closed.load(std::memory_order_acquire))
        return false;

    if (!slot.sub.tryEmplace(std::forward<F>(fn)))
        return false;

    while (true)
    {
        uint32_t s = slot.runState.fetch_or(0u, std::memory_order_acq_rel);
        if (s == RS_IDLE)
        {
            if (slot.runState.compare_exchange_strong(s, RS_SCHEDULED,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire))
            {
                drainsInFlight.fetch_add(1, std::memory_order_acq_rel);
                while (!pool.enqueue([this, id, gen] { this->drainProcessQueue(id, gen); }))
                    _mm_pause();
                break;
            }
        }
        else if (s == RS_RUNNING)
        {
            if (slot.runState.compare_exchange_strong(s, RS_RESCHED,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire))
                break;
        }
        else
        {
            break;
        }
    }

    return true;
}

template <typename F>
bool ControlScheduler::postOwned(ProcessQueueId id, uint32_t gen, RefState* owner, F&& fn) noexcept
{
    if (!owner)
        return false;

    if (!owner->alive.load(std::memory_order_acquire))
        return false;

    owner->pending.fetch_add(1, std::memory_order_acq_rel);

    return post(id, gen,
        [token = PendingToken(owner), fn = std::forward<F>(fn)]() mutable noexcept
        {
            RefState* prev = tlsCurrentTaskOwner;
            tlsCurrentTaskOwner = token.state;
            fn();
            tlsCurrentTaskOwner = prev;
        });
}

template <typename F>
uint32_t ControlScheduler::schedule(ProcessQueueId id,
                                    uint32_t gen,
                                    RefState* owner,
                                    std::chrono::steady_clock::time_point expiration,
                                    F&& fn) noexcept
{
    if (stopping.load(std::memory_order_acquire))
        return 0;

    if (!id || !owner)
        return 0;

    ProcessQueueSlot& slot = *id;

    // Unguarded pre-check; see post() for the fairness rationale.
    if (!slot.active.load(std::memory_order_acquire) ||
        slot.generation.load(std::memory_order_acquire) != gen)
        return 0;

    AccessGuard guard(slot.accessors);

    if (!slot.active.load(std::memory_order_seq_cst))
        return 0;

    if (slot.generation.load(std::memory_order_seq_cst) != gen)
        return 0;

    if (slot.closed.load(std::memory_order_acquire))
        return 0;

    if (maxDelayedTimers == 0)
        return 0;

    if (!owner->alive.load(std::memory_order_acquire))
        return 0;

    const uint32_t delayedIdx = allocDelayedSlot();
    if (delayedIdx == kInvalidIndex)
        return 0;

    DelayedSlot& ds = delayedSlots[delayedIdx];
    const uint32_t delayedGen = static_cast<uint32_t>(ds.genClaim.load(std::memory_order_acquire) >> 1);
    const uint32_t publicHandle = packDelayedHandle(delayedIdx, delayedGen);

    ds.qid = id;
    ds.qgen = gen;
    ds.expiration.store(expiration.time_since_epoch().count(), std::memory_order_relaxed);

    RefTimerNode* node = new (std::nothrow) RefTimerNode();
    if (!node)
    {
        freeDelayedSlot(delayedIdx);
        return 0;
    }

    node->handle = publicHandle;

    if (!linkRefTimerNode(*owner, node))
    {
        delete node;
        freeDelayedSlot(delayedIdx);
        return 0;
    }

    ds.refNode = node;

    owner->pending.fetch_add(1, std::memory_order_acq_rel);

    ds.task.set(
        [token = PendingToken(owner), fn = std::forward<F>(fn), publicHandle]() mutable noexcept
        {
            RefState* prev = tlsCurrentTaskOwner;
            tlsCurrentTaskOwner = token.state;
            fn(publicHandle);
            tlsCurrentTaskOwner = prev;
        });

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
 * @brief Handle to one serialized execution context in @ref ControlScheduler.
 * @ingroup CORE
 *
 * A single move-only class covers both roles:
 * - The handle returned by @ref ControlScheduler::create **owns** the queue:
 *   its reset()/destructor closes the queue, discards pending tasks and
 *   recycles the slot.
 * - Handles returned by @ref ref() **borrow** the queue: they can post but
 *   never destroy it, and may outlive it (posts through a stale handle are
 *   silently dropped).
 *
 * Every handle carries its own lifetime state: release() (also run by the
 * destructor) blocks until all tasks posted *through this handle* have
 * executed or been discarded, so no lambda posted through it runs past its
 * lifetime. Releasing from inside one of the handle's own tasks is safe: the
 * remaining work is pumped inline and the current task is exempted from the
 * wait.
 *
 * Concurrency: post()/postAfter()/cancel() may be called from any number of
 * threads simultaneously, but release()/reset()/destruction must not run
 * concurrently with other calls on the *same* handle object.
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
          gen(o.gen),
          state(o.state),
          owning(o.owning)
    {
        o.engine = nullptr;
        o.id = nullptr;
        o.gen = 0;
        o.state = nullptr;
        o.owning = false;
    }

    ProcessQueue& operator=(ProcessQueue&& o) noexcept
    {
        if (this == &o)
            return *this;

        reset();

        engine = o.engine;
        id = o.id;
        gen = o.gen;
        state = o.state;
        owning = o.owning;

        o.engine = nullptr;
        o.id = nullptr;
        o.gen = 0;
        o.state = nullptr;
        o.owning = false;

        return *this;
    }

    /**
     * @brief Creates a borrowing handle for this queue with its own,
     * independently releasable lifetime.
     */
    ProcessQueue ref() const noexcept
    {
        if (!engine)
            return ProcessQueue();

        return ProcessQueue(*engine, id, gen, false);
    }

    /**
     * @brief Posts a task to the queue, guarded by this handle's
     * lifetime.
     * @return true if enqueued; false if the queue is closed or full.
     */
    template <typename F>
    bool post(F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, state, std::forward<F>(fn));
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

        bool posted = engine->postOwned(id, gen, state,
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
     * @brief Schedules a task to run at `expiration`, guarded by this
     * handle's lifetime. The callback receives its own timer handle.
     * @return Non-zero timer handle, or 0 on failure.
     */
    template <typename F>
    uint32_t postAfter(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, state, expiration, std::forward<F>(fn));
    }

    /** @brief Cancels a pending delayed task; true if cancelled before firing. */
    bool cancel(uint32_t timerId) const noexcept
    {
        if (!engine)
            return false;

        return engine->cancelDelayed(timerId);
    }

    /**
     * @brief Releases this handle's posting lifetime: cancels its pending
     * timers and blocks until tasks posted through it have executed or been
     * discarded. Safe to call multiple times, and safe to call from inside
     * one of this handle's own tasks (the remaining work is pumped inline).
     *
     * An owning handle keeps the queue alive; posting through this handle is
     * rejected afterward, but reset() still destroys the queue.
     */
    void release() noexcept
    {
        if (!state)
        {
            if (!owning)
            {
                engine = nullptr;
                id = nullptr;
                gen = 0;
            }
            return;
        }

        state->alive.store(false, std::memory_order_release);

        if (engine)
            engine->cancelRefTimers(state);

        const bool insideOwnTask = (ControlScheduler::tlsCurrentTaskOwner == state);
        const uint32_t target = insideOwnTask ? 1u : 0u;

        uint32_t v = state->pending.load(std::memory_order_acquire);
        while (v > target)
        {
            if (engine && engine->onDrainThread(id))
            {
                engine->pumpOwnDrainThread(id);
                v = state->pending.load(std::memory_order_acquire);
                if (v > target)
                    _mm_pause();
                continue;
            }

            state->pending.wait(v, std::memory_order_relaxed);
            v = state->pending.load(std::memory_order_acquire);
        }

        if (insideOwnTask)
            state->detached.store(true, std::memory_order_release);
        else
            delete state;

        state = nullptr;
        if (!owning)
        {
            engine = nullptr;
            id = nullptr;
            gen = 0;
        }
    }

    /** @brief Blocks until all pending and in-flight tasks have completed. */
    void waitIdle() const noexcept
    {
        if (engine)
            engine->waitIdle(id, gen);
    }

    /** @brief Like waitIdle() but also waits (oldest-first) for every already-due postAfter task and the near-term chains they arm; future timers are excluded so periodic re-arms can't extend it, and it returns early on this queue's own drain thread. */
    void waitScheduled() const noexcept
    {
        if (engine)
            engine->waitScheduled(id, gen);
    }

    /**
     * @brief Releases this handle and, if it owns the queue, destroys the
     * queue: pending but unstarted tasks are discarded and the slot is
     * recycled. Safe to call multiple times, and safe to call from a task
     * running on this queue (destruction completes after the task returns).
     */
    void reset() noexcept
    {
        if (owning && engine)
            engine->destroyProcessQueue(id, gen);

        release();

        engine = nullptr;
        id = nullptr;
        gen = 0;
        owning = false;
    }

    ControlScheduler::ProcessQueueId getId() const noexcept { return id; }
    uint32_t generation() const noexcept { return gen; }

private:
    ProcessQueue(ControlScheduler& e,
                 ControlScheduler::ProcessQueueId qid,
                 uint32_t qgen,
                 bool owns) noexcept
        : engine(&e),
          id(qid),
          gen(qgen),
          state(new ControlScheduler::RefState()),
          owning(owns)
    {}

private:
    ControlScheduler* engine = nullptr;             ///< Null for a default-constructed or moved-from handle.
    ControlScheduler::ProcessQueueId id = nullptr;
    uint32_t gen = 0;                               ///< Guards against slot reuse (ABA).
    ControlScheduler::RefState* state = nullptr;    ///< This handle's posting-lifetime state.
    bool owning = false;                            ///< True only for the handle returned by create().

    INJECT_MOCK(MOCK_PROCESS_QUEUE_MUTEX_GETTER)
};
} // namespace core

#endif // CONTROL_ENGINE_H
