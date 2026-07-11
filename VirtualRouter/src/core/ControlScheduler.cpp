// ControlScheduler.cpp

#include <bit>

#include "ControlScheduler.h"

namespace core
{

namespace
{

thread_local int gControlSchedulerTlsMarker = 0;

/// Decrements a counter on scope exit; wakes waiters when it reaches zero.
struct InFlightGuard
{
    std::atomic<uint32_t>& counter;

    explicit InFlightGuard(std::atomic<uint32_t>& c) noexcept
        : counter(c)
    {
        counter.fetch_add(1, std::memory_order_acq_rel);
    }

    ~InFlightGuard() noexcept
    {
        if (counter.fetch_sub(1, std::memory_order_acq_rel) == 1)
            counter.notify_all();
    }
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

} // namespace

void ControlScheduler::SubQueue::init(uint32_t cap)
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

void ControlScheduler::SubQueue::reset() noexcept
{
    if (slots)
    {
        discardAll();
        delete[] slots;
        slots = nullptr;
    }

    capacity = 0;
    mask = 0;
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
}

bool ControlScheduler::SubQueue::tryConsumeOne(bool run) noexcept
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

bool ControlScheduler::SubQueue::hasItem() const noexcept
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
        delayedFreeHead.store(0, std::memory_order_relaxed);

        for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
            delayedSlots[i].nextFree.store((i + 1 < maxDelayedTimers) ? (i + 1) : kInvalidIndex, std::memory_order_relaxed);
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

    // A queue destroyed from within its own drain task (deferred destroy) can
    // leave that drain still finishing on a pool thread; wait it out before
    // freeing the slot arrays it touches.
    uint32_t v = drainsInFlight.load(std::memory_order_acquire);
    while (v != 0)
    {
        drainsInFlight.wait(v, std::memory_order_relaxed);
        v = drainsInFlight.load(std::memory_order_acquire);
    }

    // In-use slots here mean a ref outlived the scheduler (contract
    // violation); clean up the tasks but leave the nodes to the ref, which
    // still holds them in its list.
    for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
    {
        if (delayedSlots[i].inUse.load(std::memory_order_acquire))
            delayedSlots[i].task.cleanup();
    }

    delete[] delayedSlots;

    for (ProcessQueueSlot* node : pqAllNodes)
        delete node;
}

ProcessQueue ControlScheduler::create(uint32_t cap, std::initializer_list<SubQueueConfig> labeled)
{
    validatePow2(cap);

    if (1 + labeled.size() > kMaxSubQueues)
        throw std::runtime_error("Too many subqueues for this processqueue.");

    for (const auto& cfg : labeled)
        validatePow2(cfg.capacity);

    for (auto it = labeled.begin(); it != labeled.end(); ++it)
    {
        for (auto prev = labeled.begin(); prev != it; ++prev)
        {
            if (prev->label == it->label)
                throw std::runtime_error("Duplicate subqueue label");
        }
    }

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

    ProcessQueueState& st = slot.state;
    st.resetConfig();

    for (uint16_t i = 0; i < kMaxSubQueues; ++i)
        st.sub[i].reset();

    st.sub[0].init(cap);
    st.subCount = 1;

    for (const auto& cfg : labeled)
    {
        st.sub[st.subCount].init(cfg.capacity);
        st.labels[st.labelCount++] = LabelMapEntry{cfg.label, st.subCount};
        ++st.subCount;
    }

    slot.active.store(true, std::memory_order_release);

    return ProcessQueue(*this, id, gen);
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
    uint32_t head = delayedFreeHead.load(std::memory_order_acquire);

    while (head != kInvalidIndex)
    {
        DelayedSlot& ds = delayedSlots[head];
        const uint32_t next = ds.nextFree.load(std::memory_order_relaxed);

        if (delayedFreeHead.compare_exchange_weak(
                head,
                next,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            ds.nextFree.store(kInvalidIndex, std::memory_order_relaxed);
            ds.inUse.store(true, std::memory_order_release);
            ds.completed.store(false, std::memory_order_release);
            ds.tmTimerId.store(0, std::memory_order_release);
            ds.refNode = nullptr;
            return head;
        }
    }

    return kInvalidIndex;
}

void ControlScheduler::freeDelayedSlot(uint32_t idx) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    ds.qid = 0;
    ds.qgen = 0;
    ds.hasLabel = false;
    ds.label = Label{0};
    ds.refNode = nullptr;
    ds.tmTimerId.store(0, std::memory_order_relaxed);
    ds.completed.store(false, std::memory_order_relaxed);

    const uint32_t curGen = ds.generation.load(std::memory_order_relaxed);
    ds.generation.store(nextDelayedGeneration(curGen), std::memory_order_release);
    ds.inUse.store(false, std::memory_order_release);

    uint32_t head = delayedFreeHead.load(std::memory_order_relaxed);
    do
    {
        ds.nextFree.store(head, std::memory_order_relaxed);
    }
    while (!delayedFreeHead.compare_exchange_weak(
        head,
        idx,
        std::memory_order_release,
        std::memory_order_relaxed));
}

void ControlScheduler::finishFiredDelayed(uint32_t idx, uint32_t expectedGen, bool run) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    if (!ds.inUse.load(std::memory_order_acquire))
        return;

    if (ds.generation.load(std::memory_order_acquire) != expectedGen)
        return;

    // onTimerFired() already detached ds.refNode before handing us the slot.
    if (run)
        ds.task.run();

    ds.task.cleanup();
    freeDelayedSlot(idx);
}

void ControlScheduler::onTimerFired(uint32_t delayedIdx, uint32_t delayedGen) noexcept
{
    InFlightGuard guard(timerInFlight);

    if (delayedIdx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[delayedIdx];

    if (!ds.inUse.load(std::memory_order_acquire))
        return;

    if (ds.generation.load(std::memory_order_acquire) != delayedGen)
        return;

    if (ds.completed.exchange(true, std::memory_order_acq_rel))
        return;

    // We own this firing. schedule() publishes tmTimerId as its *final* step;
    // wait for it so the slot cannot be freed (and reallocated) below while
    // the scheduling thread still has one store outstanding.
    while (ds.tmTimerId.load(std::memory_order_acquire) == 0)
        _mm_pause();

    // Detach the timer node *now*, while timerInFlight still excludes
    // destroyRefState() from deleting it. The trampoline posted below runs
    // asynchronously and must never touch the node.
    if (ds.refNode)
    {
        ds.refNode->active.store(false, std::memory_order_release);
        ds.refNode = nullptr;
    }

    const ProcessQueueId qid = ds.qid;
    const uint32_t qgen = ds.qgen;
    std::optional<Label> label;
    if (ds.hasLabel)
        label = ds.label;

    // Discards the fired task (freeing its slot) unless the trampoline ran it.
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
                engine->finishFiredDelayed(idx, gen, false);
        }
    };

    const bool ok = post(qid, qgen, label,
        [this, delayedIdx, delayedGen, tok = FiredDelayedToken(this, delayedIdx, delayedGen)]() mutable noexcept
        {
            finishFiredDelayed(delayedIdx, delayedGen, true);
            tok.released = true;
        });

    // On failure the slot is freed at most once: the lambda's token already
    // discarded it, and this call fails its generation check.
    if (!ok)
        finishFiredDelayed(delayedIdx, delayedGen, false);
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

    if (!ds.inUse.load(std::memory_order_acquire))
        return false;

    if (ds.generation.load(std::memory_order_acquire) != gen)
        return false;

    if (ds.completed.exchange(true, std::memory_order_acq_rel))
        return false;

    // We own the cancellation. schedule() publishes tmTimerId as its final
    // step; wait for it so freeing the slot below cannot race that store
    // (reachable via destroyRefState() cancelling a mid-flight schedule()).
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
        ds.refNode = nullptr;
    }

    freeDelayedSlot(idx);
    return true;
}

bool ControlScheduler::linkRefTimerNode(ProcessQueueRefState& owner, RefTimerNode* node) noexcept
{
    while (owner.listLock.test_and_set(std::memory_order_acquire))
        _mm_pause();

    // Re-check under the lock: destroyRefState() detaches the list under this
    // lock after `alive` is already false, so a successful link here is
    // guaranteed to be visible to (and cancelled by) the release path.
    if (!owner.alive.load(std::memory_order_acquire))
    {
        owner.listLock.clear(std::memory_order_release);
        return false;
    }

    // Prune nodes whose timers already fired or were cancelled; both paths
    // store `active = false` as their final access, so deletion is safe.
    RefTimerNode** link = &owner.timerHead;
    while (RefTimerNode* cur = *link)
    {
        if (!cur->active.load(std::memory_order_acquire))
        {
            *link = cur->next;
            delete cur;
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

void ControlScheduler::destroyRefState(ProcessQueueRefState* state) noexcept
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

    for (RefTimerNode* cur = head; cur; cur = cur->next)
    {
        if (cur->active.load(std::memory_order_acquire))
            cancelDelayed(cur->handle);
    }

    // A concurrently-firing timer that beat one of the cancels above may
    // still be touching its node. Such callbacks are counted in timerInFlight
    // from their first instruction, so waiting makes the deletions safe.
    uint32_t v = timerInFlight.load(std::memory_order_acquire);
    while (v != 0)
    {
        timerInFlight.wait(v, std::memory_order_relaxed);
        v = timerInFlight.load(std::memory_order_acquire);
    }

    while (head)
    {
        RefTimerNode* next = head->next;
        delete head;
        head = next;
    }
}

void ControlScheduler::drainProcessQueue(ProcessQueueId id, uint32_t gen) noexcept
{
    InFlightGuard guard(drainsInFlight);

    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    if (!slot.active.load(std::memory_order_acquire))
        return;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    ProcessQueueState& st = slot.state;

    st.draining.store(true, std::memory_order_release);
    st.drainThreadMarker.store(&gControlSchedulerTlsMarker, std::memory_order_relaxed);
    st.waitCv.notify_all();

    uint16_t rr = 0;

    while (true)
    {
        // Consume tasks round-robin across sub-queues until all are empty.
        while (true)
        {
            const bool run = !st.closed.load(std::memory_order_acquire);
            const uint16_t n = st.subCount;
            bool consumed = false;

            for (uint16_t k = 0; k < n; ++k)
            {
                const uint16_t idx = static_cast<uint16_t>((rr + k) % n);
                if (st.sub[idx].tryConsumeOne(run))
                {
                    rr = static_cast<uint16_t>((idx + 1) % n);
                    consumed = true;
                    break;
                }
            }

            if (!consumed || st.closed.load(std::memory_order_acquire))
                break;
        }

        st.scheduled.store(false, std::memory_order_release);

        if (st.closed.load(std::memory_order_acquire))
        {
            for (uint16_t i = 0; i < st.subCount; ++i)
                st.sub[i].discardAll();
            break;
        }

        if (!st.hasAnyPending())
            break;

        // Items arrived after we cleared `scheduled`: re-claim it and keep
        // draining, unless a poster already scheduled a new drain.
        bool expected = false;
        if (!st.scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            break;
    }

    st.drainThreadMarker.store(nullptr, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(st.waitMtx);
        st.draining.store(false, std::memory_order_release);
    }
    st.waitCv.notify_all();

    if (st.deferDestroy.load(std::memory_order_acquire))
        finalizeDestroy(id, gen);
}

void ControlScheduler::waitIdle(ProcessQueueId id, uint32_t gen) noexcept
{
    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    if (!slot.active.load(std::memory_order_acquire))
        return;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    ProcessQueueState& st = slot.state;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lk(st.waitMtx);
            st.waitCv.wait(lk, [&] {
                return !st.draining.load(std::memory_order_acquire) &&
                       !st.scheduled.load(std::memory_order_acquire);
            });
        }

        if (slot.generation.load(std::memory_order_acquire) != gen)
            return;

        if (!st.hasAnyPending())
            return;

        std::this_thread::yield();
    }
}

void ControlScheduler::destroyProcessQueue(ProcessQueueId id, uint32_t gen) noexcept
{
    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    if (!slot.active.load(std::memory_order_acquire))
        return;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    ProcessQueueState& st = slot.state;

    st.closed.store(true, std::memory_order_release);

    // Destroy from within our own drain task: defer to the drain loop, which
    // finalizes after it returns (waiting here would deadlock).
    if (st.drainThreadMarker.load(std::memory_order_acquire) == &gControlSchedulerTlsMarker)
    {
        st.deferDestroy.store(true, std::memory_order_release);
        return;
    }

    // The queue is closed, so pending items would only be discarded — do that
    // ourselves once any in-flight drain has finished.
    {
        std::unique_lock<std::mutex> lk(st.waitMtx);
        st.waitCv.wait(lk, [&] {
            return !st.draining.load(std::memory_order_acquire) &&
                   !st.scheduled.load(std::memory_order_acquire);
        });
    }

    for (uint16_t i = 0; i < st.subCount; ++i)
        st.sub[i].discardAll();

    finalizeDestroy(id, gen);
}

void ControlScheduler::finalizeDestroy(ProcessQueueId id, uint32_t gen) noexcept
{
    if (!id)
        return;

    ProcessQueueSlot& slot = *id;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    // Claim finalization atomically: destroyProcessQueue() and a deferred
    // destroy on the drain thread can both reach this point; only one may
    // reset the slot.
    if (!slot.active.exchange(false, std::memory_order_acq_rel))
        return;

    // Invalidate the generation before draining posters, so any post that we
    // then wait for (or that starts later) fails its generation re-check.
    slot.generation.fetch_add(1, std::memory_order_seq_cst);

    ProcessQueueState& st = slot.state;

    // Wait out threads still inside post()/schedule(); they must not touch
    // the sub-queue rings once we start freeing them.
    while (st.posters.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

    for (uint16_t i = 0; i < st.subCount; ++i)
        st.sub[i].reset();

    st.resetConfig();

    {
        std::lock_guard<std::mutex> g(pqAllocMtx);
        pqFreeNodes.push_back(id);
    }
}

} // namespace core
