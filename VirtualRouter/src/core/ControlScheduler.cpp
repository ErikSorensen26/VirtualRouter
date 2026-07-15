// ControlScheduler.cpp

#include <bit>
#include <thread>

#include "ControlScheduler.h"

namespace core
{

namespace
{

/// Unique-per-thread address used to recognize the drain thread of a queue.
thread_local int gControlSchedulerTlsMarker = 0;

/// Decrements a counter on scope exit; wakes waiters when it reaches zero.
/// The matching increment happens where the work is *enqueued*, so waiters
/// also cover work that is queued but not yet running.
struct CounterDoneGuard
{
    std::atomic<uint32_t>& counter;

    explicit CounterDoneGuard(std::atomic<uint32_t>& c) noexcept
        : counter(c)
    {}

    ~CounterDoneGuard() noexcept
    {
        if (counter.fetch_sub(1, std::memory_order_acq_rel) == 1)
            counter.notify_all();
    }

    CounterDoneGuard(const CounterDoneGuard&) = delete;
    CounterDoneGuard& operator=(const CounterDoneGuard&) = delete;
};

void validatePow2(uint32_t cap)
{
    if (cap < 2 || (cap & (cap - 1)) != 0)
        throw std::runtime_error("Queue capacity must be a power of two and >= 2");
}

uint32_t indexBitsFor(size_t maxDelayed) noexcept
{
    return maxDelayed <= 1 ? 0u : static_cast<uint32_t>(std::bit_width(maxDelayed - 1));
}

uint32_t makeIndexMask(uint32_t bits) noexcept
{
    if (bits == 0)
        return 0u;
    if (bits >= 32)
        return 0xFFFFFFFFu;
    return (1u << bits) - 1u;
}

uint32_t makeGenerationMask(uint32_t indexBits) noexcept
{
    return indexBits == 0 ? 0xFFFFFFFFu : (0xFFFFFFFFu >> indexBits);
}

/// Free-list head packing: low 32 bits = head index, high 32 bits = ABA tag.
constexpr uint64_t packFreeHead(uint64_t tag, uint32_t idx) noexcept
{
    return (tag << 32) | idx;
}

constexpr uint32_t freeHeadIndex(uint64_t head) noexcept
{
    return static_cast<uint32_t>(head);
}

constexpr uint64_t freeHeadNextTag(uint64_t head) noexcept
{
    return (head >> 32) + 1;
}

} // namespace

void ControlScheduler::TaskRing::init(uint32_t cap)
{
    reset();

    validatePow2(cap);

    capacity = cap;
    mask = cap - 1;

    slots = new Slot[capacity];

    for (uint32_t i = 0; i < capacity; ++i)
        slots[i].seq.store(static_cast<uint64_t>(i), std::memory_order_relaxed);

    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
}

void ControlScheduler::TaskRing::reset() noexcept
{
    if (slots)
    {
        while (tryConsumeOne(false)) {}
        delete[] slots;
        slots = nullptr;
    }

    capacity = 0;
    mask = 0;
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
}

bool ControlScheduler::TaskRing::tryConsumeOne(bool run) noexcept
{
    uint64_t pos = tail.load(std::memory_order_relaxed);

    while (true)
    {
        Slot* s = &slots[pos & mask];
        uint64_t seq = s->seq.load(std::memory_order_acquire);
        intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (dif == 0)
        {
            if (tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                if (run)
                    s->task.run();

                s->task.cleanup();
                s->seq.store(pos + capacity, std::memory_order_release);
                return true;
            }
        }
        else if (dif < 0)
        {
            return false;
        }
        else
        {
            pos = tail.load(std::memory_order_relaxed);
        }

        _mm_pause();
    }
}

bool ControlScheduler::TaskRing::hasItem() const noexcept
{
    const uint64_t pos = tail.load(std::memory_order_relaxed);
    const Slot* s = &slots[pos & mask];
    const uint64_t seq = s->seq.load(std::memory_order_acquire);
    return static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1) == 0;
}

ControlScheduler::ControlScheduler(core::ThreadPool& externalPool,
                                   core::TimeManager& tmgr,
                                   size_t reserveQueues,
                                   size_t maxDelayed)
    : pool(externalPool),
      timeManager(tmgr),
      maxDelayedTimers(maxDelayed),
      delayedFreeHead(packFreeHead(0, kInvalidIndex)),
      delayedIndexBits(indexBitsFor(maxDelayed)),
      delayedIndexMask(makeIndexMask(delayedIndexBits)),
      delayedGenerationMask(makeGenerationMask(delayedIndexBits))
{
    if (maxDelayedTimers > 0 && delayedGenerationMask == 0)
        throw std::runtime_error("maxDelayedTimers leaves no bits for generation in 32-bit timer handle");

    pqAllNodes.reserve(reserveQueues);
    pqFreeNodes.reserve(reserveQueues);
    for (size_t i = 0; i < reserveQueues; ++i)
    {
        ProcessQueueSlot* node = new ProcessQueueSlot();
        pqAllNodes.push_back(node);
        pqFreeNodes.push_back(node);
    }

    delayedSlots = new DelayedSlot[maxDelayedTimers];

    if (maxDelayedTimers > 0)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
            delayedSlots[i].nextFree.store((i + 1 < maxDelayedTimers) ? (i + 1) : kInvalidIndex, std::memory_order_relaxed);

        delayedFreeHead.store(packFreeHead(0, 0), std::memory_order_relaxed);
    }
}

ControlScheduler::~ControlScheduler()
{
    stopping.store(true, std::memory_order_release);

    // stopTimer() joins the timer thread AND waits for every callback already
    // dispatched to the pool, so no onTimerFired() can start (or be running)
    // after this returns.
    timeManager.stopTimer();

    for (ProcessQueueSlot* node : pqAllNodes)
    {
        if (node->active.load(std::memory_order_acquire))
        {
            const uint32_t gen = node->generation.load(std::memory_order_acquire);
            destroyProcessQueue(node, gen);
        }
    }

    // drainsInFlight counts drains from the moment they are enqueued, so this
    // also waits out drain tasks still sitting unstarted in the pool ring —
    // they must not touch the slot nodes (or this object) after we free them.
    uint32_t v = drainsInFlight.load(std::memory_order_acquire);
    while (v != 0)
    {
        drainsInFlight.wait(v, std::memory_order_relaxed);
        v = drainsInFlight.load(std::memory_order_acquire);
    }

    // In-use slots here mean a handle outlived the scheduler (contract
    // violation); clean up the tasks but leave the nodes to the handle, which
    // still holds a share of them.
    for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
    {
        if (delayedSlots[i].inUse.load(std::memory_order_acquire))
            delayedSlots[i].task.cleanup();
    }

    delete[] delayedSlots;

    for (ProcessQueueSlot* node : pqAllNodes)
        delete node;
}

ProcessQueue ControlScheduler::create(uint32_t cap)
{
    validatePow2(cap);

    ProcessQueueId id = nullptr;
    {
        std::lock_guard<std::mutex> g(pqAllocMtx);
        if (!pqFreeNodes.empty())
        {
            id = pqFreeNodes.back();
            pqFreeNodes.pop_back();
        }
    }

    if (!id)
    {
        id = new ProcessQueueSlot();

        std::lock_guard<std::mutex> g(pqAllocMtx);
        pqAllNodes.push_back(id);
    }

    ProcessQueueSlot& slot = *id;
    const uint32_t gen = slot.generation.load(std::memory_order_relaxed);

    slot.resetConfig();
    slot.sub.init(cap);

    slot.active.store(true, std::memory_order_seq_cst);

    return ProcessQueue(*this, id, gen, true);
}

uint32_t ControlScheduler::packDelayedHandle(uint32_t idx, uint32_t gen) const noexcept
{
    if (delayedIndexBits == 0)
        return gen;

    return ((gen & delayedGenerationMask) << delayedIndexBits) | (idx & delayedIndexMask);
}

uint32_t ControlScheduler::unpackDelayedIndex(uint32_t handle) const noexcept
{
    return delayedIndexBits == 0 ? 0 : (handle & delayedIndexMask);
}

uint32_t ControlScheduler::unpackDelayedGeneration(uint32_t handle) const noexcept
{
    return delayedIndexBits == 0 ? handle : ((handle >> delayedIndexBits) & delayedGenerationMask);
}

uint32_t ControlScheduler::nextDelayedGeneration(uint32_t current) const noexcept
{
    uint32_t next = (current + 1) & delayedGenerationMask;
    return next == 0 ? 1 : next;
}

uint32_t ControlScheduler::allocDelayedSlot() noexcept
{
    uint64_t head = delayedFreeHead.load(std::memory_order_acquire);

    while (true)
    {
        const uint32_t idx = freeHeadIndex(head);
        if (idx == kInvalidIndex)
            return kInvalidIndex;

        DelayedSlot& ds = delayedSlots[idx];

        // `next` may be stale if another thread pops/pushes concurrently; the
        // tag in the head CAS below detects that and retries (no ABA).
        const uint32_t next = ds.nextFree.load(std::memory_order_relaxed);

        if (delayedFreeHead.compare_exchange_weak(
                head,
                packFreeHead(freeHeadNextTag(head), next),
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            ds.nextFree.store(kInvalidIndex, std::memory_order_relaxed);
            ds.tmTimerId.store(0, std::memory_order_relaxed);
            ds.refNode = nullptr;
            ds.inUse.store(true, std::memory_order_release);
            return idx;
        }
    }
}

void ControlScheduler::freeDelayedSlot(uint32_t idx) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    ds.qid = nullptr;
    ds.qgen = 0;
    ds.refNode = nullptr;
    ds.tmTimerId.store(0, std::memory_order_relaxed);

    // Bump the generation and clear the claim in one store: stale handles
    // can no longer claim this slot, in any interleaving.
    const uint32_t curGen = static_cast<uint32_t>(ds.genClaim.load(std::memory_order_relaxed) >> 1);
    ds.genClaim.store(static_cast<uint64_t>(nextDelayedGeneration(curGen)) << 1, std::memory_order_release);
    ds.inUse.store(false, std::memory_order_release);

    uint64_t head = delayedFreeHead.load(std::memory_order_relaxed);
    do
    {
        ds.nextFree.store(freeHeadIndex(head), std::memory_order_relaxed);
    }
    while (!delayedFreeHead.compare_exchange_weak(
        head,
        packFreeHead(freeHeadNextTag(head), idx),
        std::memory_order_release,
        std::memory_order_relaxed));
}

void ControlScheduler::finishFiredDelayed(uint32_t idx, uint32_t expectedGen, bool run) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    // The caller holds the claim for this generation; anything else is a bug
    // upstream, so bail without touching the slot.
    if (ds.genClaim.load(std::memory_order_acquire) != ((static_cast<uint64_t>(expectedGen) << 1) | 1))
        return;

    // onTimerFired() already detached ds.refNode before handing us the slot.
    if (run)
        ds.task.run();

    ds.task.cleanup();
    freeDelayedSlot(idx);
}

void ControlScheduler::retireFiredDelayed(uint32_t idx, uint32_t expectedGen) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    // We still hold the fire's claim for this generation.
    if (ds.genClaim.load(std::memory_order_acquire) != ((static_cast<uint64_t>(expectedGen) << 1) | 1))
        return;

    // The fired task never ran: either its trampoline could not be posted
    // (ring momentarily full) or it was discarded with the queue. If the
    // queue is still alive the timer must not be lost — re-arm it shortly.
    ProcessQueueSlot* qs = ds.qid;
    const bool queueAlive = qs
        && !stopping.load(std::memory_order_acquire)
        && qs->active.load(std::memory_order_seq_cst)
        && qs->generation.load(std::memory_order_seq_cst) == ds.qgen
        && !qs->closed.load(std::memory_order_acquire);

    if (!queueAlive)
    {
        finishFiredDelayed(idx, expectedGen, false);
        return;
    }

    // Unpublish tmTimerId first so a racing cancel spins until the new timer
    // id is visible, then release the claim to re-open fire/cancel arbitration.
    ds.tmTimerId.store(0, std::memory_order_release);
    ds.genClaim.store(static_cast<uint64_t>(expectedGen) << 1, std::memory_order_release);

    const uint32_t tmId = timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1),
        [this, idx, expectedGen](uint32_t) noexcept
        {
            onTimerFired(idx, expectedGen);
        });

    ds.tmTimerId.store(tmId, std::memory_order_release);
}

void ControlScheduler::onTimerFired(uint32_t delayedIdx, uint32_t delayedGen) noexcept
{
    if (delayedIdx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[delayedIdx];

    // Claim the slot for exactly our generation; fails if it was cancelled,
    // freed or recycled in the meantime.
    uint64_t expected = static_cast<uint64_t>(delayedGen) << 1;
    if (!ds.genClaim.compare_exchange_strong(expected, expected | 1, std::memory_order_acq_rel))
        return;

    // We own this firing. schedule() publishes tmTimerId as its *final* step;
    // wait for it so the slot cannot be freed (and reallocated) below while
    // the scheduling thread still has one store outstanding.
    while (ds.tmTimerId.load(std::memory_order_acquire) == 0)
        _mm_pause();

    // Detach the timer node: from here on release() no longer needs to cancel
    // this timer — the trampoline's pending token keeps it waiting instead.
    if (ds.refNode)
    {
        ds.refNode->active.store(false, std::memory_order_release);
        dropNodeOwner(ds.refNode);
        ds.refNode = nullptr;
    }

    const ProcessQueueId qid = ds.qid;
    const uint32_t qgen = ds.qgen;

    // Retires the fired task (re-arming or freeing its slot) unless the
    // trampoline actually ran it. Covers both a failed post below and a
    // trampoline discarded from the ring without running.
    struct FiredDelayedToken
    {
        ControlScheduler* engine;
        uint32_t idx;
        uint32_t gen;
        bool released = false;

        FiredDelayedToken(ControlScheduler* e, uint32_t i, uint32_t g) noexcept
            : engine(e), idx(i), gen(g)
        {}

        FiredDelayedToken(const FiredDelayedToken&) = delete;
        FiredDelayedToken& operator=(const FiredDelayedToken&) = delete;

        FiredDelayedToken(FiredDelayedToken&& o) noexcept
            : engine(o.engine), idx(o.idx), gen(o.gen), released(o.released)
        {
            o.released = true;
        }

        ~FiredDelayedToken() noexcept
        {
            if (!released)
                engine->retireFiredDelayed(idx, gen);
        }
    };

    post(qid, qgen,
        [this, delayedIdx, delayedGen, tok = FiredDelayedToken(this, delayedIdx, delayedGen)]() mutable noexcept
        {
            tok.released = true;
            finishFiredDelayed(delayedIdx, delayedGen, true);
        });
    // On post failure the lambda above is destroyed on the spot and its token
    // retires the slot exactly once.
}

bool ControlScheduler::cancelDelayed(uint32_t timerId) noexcept
{
    if (timerId == 0 || maxDelayedTimers == 0)
        return false;

    const uint32_t idx = unpackDelayedIndex(timerId);
    const uint32_t gen = unpackDelayedGeneration(timerId);

    if (idx >= maxDelayedTimers)
        return false;

    DelayedSlot& ds = delayedSlots[idx];

    // Claim the slot for exactly the handle's generation; fails if already
    // fired/cancelled, freed, or recycled (no window for a stale handle to
    // claim someone else's slot).
    uint64_t expected = static_cast<uint64_t>(gen) << 1;
    if (!ds.genClaim.compare_exchange_strong(expected, expected | 1, std::memory_order_acq_rel))
        return false;

    // We own the cancellation. schedule() (and the re-arm path) publish
    // tmTimerId as their final step; wait for it so freeing the slot below
    // cannot race that store.
    uint32_t tmId = ds.tmTimerId.load(std::memory_order_acquire);
    while (tmId == 0)
    {
        _mm_pause();
        tmId = ds.tmTimerId.load(std::memory_order_acquire);
    }

    timeManager.cancelTimer(tmId);

    ds.task.cleanup();

    if (ds.refNode)
    {
        ds.refNode->active.store(false, std::memory_order_release);
        dropNodeOwner(ds.refNode);
        ds.refNode = nullptr;
    }

    freeDelayedSlot(idx);
    return true;
}

bool ControlScheduler::linkRefTimerNode(RefState& owner, RefTimerNode* node) noexcept
{
    while (owner.listLock.test_and_set(std::memory_order_acquire))
        _mm_pause();

    // Re-check under the lock: cancelRefTimers() detaches the list under this
    // lock after `alive` is already false, so a successful link here is
    // guaranteed to be visible to (and cancelled by) the release path.
    if (!owner.alive.load(std::memory_order_acquire))
    {
        owner.listLock.clear(std::memory_order_release);
        return false;
    }

    // Prune nodes whose timers already fired or were cancelled, dropping the
    // list's ownership share; the timer side dropped its own share when it
    // marked the node inactive.
    RefTimerNode** link = &owner.timerHead;
    while (RefTimerNode* cur = *link)
    {
        if (!cur->active.load(std::memory_order_acquire))
        {
            *link = cur->next;
            dropNodeOwner(cur);
        }
        else
        {
            link = &cur->next;
        }
    }

    node->next = owner.timerHead;
    owner.timerHead = node;

    owner.listLock.clear(std::memory_order_release);
    return true;
}

void ControlScheduler::cancelRefTimers(RefState* state) noexcept
{
    if (!state)
        return;

    // Detach the whole list under the lock. `alive` is already false, so no
    // new node can be linked after this (linkRefTimerNode re-checks it under
    // the same lock).
    RefTimerNode* head;
    {
        while (state->listLock.test_and_set(std::memory_order_acquire))
            _mm_pause();

        head = state->timerHead;
        state->timerHead = nullptr;

        state->listLock.clear(std::memory_order_release);
    }

    while (head)
    {
        RefTimerNode* next = head->next;

        if (head->active.load(std::memory_order_acquire))
            cancelDelayed(head->handle);

        // A fire that beat the cancel still holds its own ownership share, so
        // dropping ours here can never free a node under it.
        dropNodeOwner(head);
        head = next;
    }
}

bool ControlScheduler::consumeOne(ProcessQueueSlot& slot) noexcept
{
    const bool run = !slot.closed.load(std::memory_order_acquire);
    return slot.sub.tryConsumeOne(run);
}

void ControlScheduler::drainProcessQueue(ProcessQueueId id, uint32_t gen) noexcept
{
    // Matches the increment done where this drain was enqueued (post()).
    CounterDoneGuard done(drainsInFlight);

    if (!id)
        return;

    ProcessQueueSlot& slot = *id;
    bool defer = false;

    {
        AccessGuard guard(slot.accessors);

        if (!slot.active.load(std::memory_order_seq_cst))
            return;

        if (slot.generation.load(std::memory_order_seq_cst) != gen)
            return;

        // We are the single scheduled drain; claim RUNNING so posters flag
        // late arrivals via RS_RESCHED instead of scheduling a second drain.
        // The exchange acquires the release sequence of every poster RMW on
        // runState, making all items published before it visible to our scan.
        slot.runState.exchange(RS_RUNNING, std::memory_order_acq_rel);
        slot.drainThreadMarker.store(&gControlSchedulerTlsMarker, std::memory_order_release);

        while (true)
        {
            while (consumeOne(slot)) {}

            // Clear the marker before we can go idle: once IDLE is published
            // another thread may start the next drain and own the marker.
            slot.drainThreadMarker.store(nullptr, std::memory_order_relaxed);

            uint32_t expected = RS_RUNNING;
            bool idle;
            {
                std::lock_guard<std::mutex> lk(slot.waitMtx);
                idle = slot.runState.compare_exchange_strong(expected, RS_IDLE,
                                                             std::memory_order_acq_rel);
            }
            if (idle)
                break;

            // A poster flagged RS_RESCHED after our last empty scan: its item
            // is published, so another pass is guaranteed to find it.
            //
            // This transition MUST be an RMW, not a plain store: a buffered
            // store could still be invisible while the rescan's loads already
            // execute, so a poster could read the stale RESCHED (assuming a
            // rescan is still owed) after the rescan effectively happened,
            // and its item would be lost. The RMW commits before our loads
            // and synchronizes with the poster RMW that set RESCHED, making
            // every item published before it visible to the rescan.
            slot.runState.exchange(RS_RUNNING, std::memory_order_acq_rel);
            slot.drainThreadMarker.store(&gControlSchedulerTlsMarker, std::memory_order_relaxed);
        }

        slot.waitCv.notify_all();

        defer = slot.deferDestroy.load(std::memory_order_acquire);
    }

    // A task we ran called destroy on its own queue; finish the job now that
    // the drain no longer holds the slot's access guard.
    if (defer)
        finalizeDestroy(id, gen);
}

bool ControlScheduler::onDrainThread(ProcessQueueId id) const noexcept
{
    if (!id)
        return false;

    return id->drainThreadMarker.load(std::memory_order_acquire) == &gControlSchedulerTlsMarker;
}

bool ControlScheduler::pumpOwnDrainThread(ProcessQueueId id) noexcept
{
    if (!onDrainThread(id))
        return false;

    // We are inside a task on this queue's drain thread, so the drain's
    // access guard already protects the ring; consuming here is the same as
    // the drain doing it (discarding instead of running once closed).
    return consumeOne(*id);
}

void ControlScheduler::waitIdle(ProcessQueueId id, uint32_t gen) noexcept
{
    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    std::unique_lock<std::mutex> lk(slot.waitMtx);
    slot.waitCv.wait(lk, [&]() noexcept
    {
        AccessGuard guard(slot.accessors);

        if (!slot.active.load(std::memory_order_seq_cst))
            return true;

        if (slot.generation.load(std::memory_order_seq_cst) != gen)
            return true;

        return slot.runState.load(std::memory_order_acquire) == RS_IDLE &&
               !slot.sub.hasItem();
    });
}

void ControlScheduler::destroyProcessQueue(ProcessQueueId id, uint32_t gen) noexcept
{
    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    {
        AccessGuard guard(slot.accessors);

        if (!slot.active.load(std::memory_order_seq_cst))
            return;

        if (slot.generation.load(std::memory_order_seq_cst) != gen)
            return;

        slot.closed.store(true, std::memory_order_seq_cst);

        // Destroy from within our own drain: defer to the drain loop, which
        // finalizes after it returns (finalizing here would deadlock waiting
        // for our own access guard).
        if (slot.drainThreadMarker.load(std::memory_order_acquire) == &gControlSchedulerTlsMarker)
        {
            slot.deferDestroy.store(true, std::memory_order_release);
            return;
        }
    }

    finalizeDestroy(id, gen);
}

void ControlScheduler::finalizeDestroy(ProcessQueueId id, uint32_t gen) noexcept
{
    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    // Claim destruction and invalidate the generation in one atomic step:
    // exactly one finalizer can win, and every accessor that saw the old
    // generation is covered by the accessors-drain wait below.
    uint32_t expected = gen;
    if (!slot.generation.compare_exchange_strong(expected, gen + 1, std::memory_order_seq_cst))
        return;

    slot.active.store(false, std::memory_order_seq_cst);

    // Wait out every thread still inside post()/schedule()/drain/waitIdle for
    // this slot; none may touch the task ring once we free it. The
    // running drain finishes (or discards) its current tasks first, so a
    // reset() from another thread blocks until in-flight work is done.
    for (uint32_t spins = 0; slot.accessors.load(std::memory_order_seq_cst) != 0; ++spins)
    {
        if (spins < 1024)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    slot.sub.reset();

    slot.resetConfig();

    {
        std::lock_guard<std::mutex> g(pqAllocMtx);
        pqFreeNodes.push_back(id);
    }

    // Wake waitIdle() callers so they re-check and observe the bumped
    // generation. The empty lock pairs with the waiters' predicate check.
    {
        std::lock_guard<std::mutex> lk(slot.waitMtx);
    }
    slot.waitCv.notify_all();
}

} // namespace core
