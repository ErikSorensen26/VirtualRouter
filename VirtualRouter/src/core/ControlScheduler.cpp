// ControlScheduler.cpp

#include <array>

#include "ControlScheduler.h"

namespace core
{

static thread_local int gcontrolEngineTlsMarker = 0;

static void isValidPow2(uint32_t cap) noexcept
{
    assert(cap >= 2 && (cap & (cap - 1)) == 0);
}

static uint32_t ceilLog2NonZero(size_t v) noexcept
{
    if (v <= 1)
        return 0;

    uint32_t bits = 0;
    size_t x = 1;
    while (x < v)
    {
        x <<= 1;
        ++bits;
    }
    return bits;
}

static uint32_t makeIndexMask(uint32_t bits) noexcept
{
    if (bits == 0)
        return 0u;
    if (bits >= 32)
        return 0xFFFFFFFFu;
    return (1u << bits) - 1u;
}

static uint32_t makeGenerationMask(uint32_t indexBits) noexcept
{
    if (indexBits == 0)
        return 0xFFFFFFFFu;

    return 0xFFFFFFFFu >> indexBits;
}

void ControlScheduler::SubQueue::init(uint32_t cap)
{
    reset();

    isValidPow2(cap);

    if (cap == 0)
        throw std::runtime_error("SubQueue capacity must be non-zero");

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
    const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
    return (dif == 0);
}

ControlScheduler::ControlScheduler(core::ThreadPool& externalPool,
                                   core::TimeManager& tmgr,
                                   size_t maxQueues,
                                   size_t maxDelayed)
    : pool(externalPool),
      timeManager(tmgr),
      maxProcessQueues(maxQueues),
      maxDelayedTimers(maxDelayed),
      delayedIndexBits(ceilLog2NonZero(maxDelayed)),
      delayedIndexMask(makeIndexMask(delayedIndexBits)),
      delayedGenerationMask(makeGenerationMask(delayedIndexBits))
{
    if (maxDelayedTimers > 0 && delayedGenerationMask == 0)
        throw std::runtime_error("maxDelayedTimers leaves no bits for generation in 32-bit timer handle");

    pqSlots = new ProcessQueueSlot[maxProcessQueues];

    pqFreeIds.reserve(maxProcessQueues);
    for (ProcessQueueId i = 0; i < static_cast<ProcessQueueId>(maxProcessQueues); ++i)
        pqFreeIds.push_back(i);

    delayedSlots = new DelayedSlot[maxDelayedTimers];

    if (maxDelayedTimers > 0)
    {
        delayedFreeHead.store(0, std::memory_order_relaxed);

        for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
        {
            delayedSlots[i].inUse.store(false, std::memory_order_relaxed);
            delayedSlots[i].completed.store(false, std::memory_order_relaxed);
            delayedSlots[i].generation.store(1, std::memory_order_relaxed);
            delayedSlots[i].tmTimerId.store(0, std::memory_order_relaxed);
            delayedSlots[i].nextFree.store((i + 1 < maxDelayedTimers) ? (i + 1) : kInvalidIndex, std::memory_order_relaxed);
            delayedSlots[i].owner = nullptr;
            delayedSlots[i].refNode = nullptr;
        }
    }
    else
    {
        delayedFreeHead.store(kInvalidIndex, std::memory_order_relaxed);
    }
}

ControlScheduler::~ControlScheduler()
{
    stopping.store(true, std::memory_order_release);
    timeManager.stopTimer();

    uint32_t v = timerInFlight.load(std::memory_order_acquire);
    if (v != 0)
    {
        timerInFlight.wait(v, std::memory_order_relaxed);
        v = timerInFlight.load(std::memory_order_acquire);
        (void)v;
    }

    for (ProcessQueueId i = 0; i < static_cast<ProcessQueueId>(maxProcessQueues); ++i)
    {
        if (pqSlots[i].active.load(std::memory_order_acquire))
        {
            const uint32_t gen = pqSlots[i].generation.load(std::memory_order_acquire);
            destroyProcessQueue(i, gen);
        }
    }

    if (delayedSlots)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
        {
            if (delayedSlots[i].inUse.load(std::memory_order_acquire))
            {
                delayedSlots[i].task.cleanup();

                if (delayedSlots[i].refNode)
                {
                    delayedSlots[i].refNode->active.store(false, std::memory_order_release);
                    delete delayedSlots[i].refNode;
                    delayedSlots[i].refNode = nullptr;
                }
            }
        }

        delete[] delayedSlots;
        delayedSlots = nullptr;
    }

    delete[] pqSlots;
    pqSlots = nullptr;
}

ProcessQueue ControlScheduler::create(uint32_t cap, std::initializer_list<SubQueueConfig> labeled)
{
    isValidPow2(cap);

    const size_t wantSub = 1 + labeled.size();
    if (wantSub > kMaxSubQueues)
        throw std::runtime_error("Too many subqueues for this processqueue.");

    for (const auto& cfg : labeled)
        isValidPow2(cfg.capacity);

    ProcessQueueId id = 0;
    uint32_t gen = 0;

    {
        std::lock_guard<std::mutex> g(pqAllocMtx);
        if (pqFreeIds.empty())
            throw std::runtime_error("No free processqueue slots");

        id = pqFreeIds.back();
        pqFreeIds.pop_back();
    }

    ProcessQueueSlot& slot = pqSlots[id];
    gen = slot.generation.load(std::memory_order_relaxed);

    ProcessQueueState& st = slot.state;
    st.resetConfig();

    for (uint16_t i = 0; i < kMaxSubQueues; ++i)
        st.sub[i].reset();

    st.sub[0].init(cap);
    st.subCount = 1;

    std::array<std::pair<uint16_t, uint32_t>, kMaxSubQueues - 1> tmp{};
    uint16_t tmpCount = 0;

    for (const auto& cfg : labeled)
        tmp[tmpCount++] = {cfg.label, cfg.capacity};

    std::sort(tmp.begin(), tmp.begin() + tmpCount,
        [](const auto& a, const auto& b) { return a.first < b.first; });

    uint16_t subIndex = 1;
    for (uint16_t i = 0; i < tmpCount; ++i)
    {
        const uint16_t label = tmp[i].first;
        const uint32_t cap2 = tmp[i].second;

        st.sub[subIndex].init(cap2);
        st.labels[st.labelCount++] = LabelMapEntry{label, subIndex};
        ++subIndex;
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
    if (delayedIndexBits == 0)
        return 0;

    return handle & delayedIndexMask;
}

uint32_t ControlScheduler::unpackDelayedGeneration(uint32_t handle) const noexcept
{
    if (delayedIndexBits == 0)
        return handle;

    return (handle >> delayedIndexBits) & delayedGenerationMask;
}

uint32_t ControlScheduler::nextDelayedGeneration(uint32_t current) const noexcept
{
    uint32_t next = current + 1;

    if (delayedIndexBits != 0)
        next &= delayedGenerationMask;

    if (next == 0)
        next = 1;

    return next;
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
            ds.owner = nullptr;
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
    ds.owner = nullptr;
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

void ControlScheduler::runDelayedByIndex(uint32_t idx, uint32_t expectedGen) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    if (!ds.inUse.load(std::memory_order_acquire))
        return;

    if (ds.generation.load(std::memory_order_acquire) != expectedGen)
        return;

    ds.task.run();
    ds.task.cleanup();

    if (ds.refNode)
    {
        ds.refNode->active.store(false, std::memory_order_release);
        ds.refNode = nullptr;
    }

    freeDelayedSlot(idx);
}

void ControlScheduler::discardFiredDelayed(uint32_t idx, uint32_t expectedGen) noexcept
{
    if (idx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[idx];

    if (!ds.inUse.load(std::memory_order_acquire))
        return;

    if (ds.generation.load(std::memory_order_acquire) != expectedGen)
        return;

    ds.task.cleanup();

    if (ds.refNode)
    {
        ds.refNode->active.store(false, std::memory_order_release);
        ds.refNode = nullptr;
    }

    freeDelayedSlot(idx);
}

void ControlScheduler::onTimerFired(uint32_t delayedIdx, uint32_t delayedGen) noexcept
{
    timerInFlight.fetch_add(1, std::memory_order_acq_rel);

    struct Guard
    {
        ControlScheduler& self;
        ~Guard() noexcept
        {
            const uint32_t prev = self.timerInFlight.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
                self.timerInFlight.notify_all();
        }
    } guard{*this};

    if (delayedIdx >= maxDelayedTimers)
        return;

    DelayedSlot& ds = delayedSlots[delayedIdx];

    if (!ds.inUse.load(std::memory_order_acquire))
        return;

    if (ds.generation.load(std::memory_order_acquire) != delayedGen)
        return;

    if (ds.completed.exchange(true, std::memory_order_acq_rel))
        return;

    if (ds.refNode)
        ds.refNode->active.store(false, std::memory_order_release);

    const ProcessQueueId qid = ds.qid;
    const uint32_t qgen = ds.qgen;
    std::optional<Label> label;
    if (ds.hasLabel)
        label = ds.label;

    struct FiredDelayedToken
    {
        ControlScheduler* engine = nullptr;
        uint32_t idx = 0;
        uint32_t gen = 0;
        bool released = false;

        FiredDelayedToken() = default;

        FiredDelayedToken(ControlScheduler* e, uint32_t i, uint32_t g) noexcept
            : engine(e), idx(i), gen(g)
        {}

        FiredDelayedToken(const FiredDelayedToken&) = delete;
        FiredDelayedToken& operator=(const FiredDelayedToken&) = delete;

        FiredDelayedToken(FiredDelayedToken&& o) noexcept
            : engine(o.engine), idx(o.idx), gen(o.gen), released(o.released)
        {
            o.engine = nullptr;
            o.released = true;
        }

        FiredDelayedToken& operator=(FiredDelayedToken&& o) noexcept
        {
            if (this == &o)
                return *this;

            if (engine && !released)
                engine->discardFiredDelayed(idx, gen);

            engine = o.engine;
            idx = o.idx;
            gen = o.gen;
            released = o.released;

            o.engine = nullptr;
            o.released = true;
            return *this;
        }

        ~FiredDelayedToken() noexcept
        {
            if (engine && !released)
                engine->discardFiredDelayed(idx, gen);
        }
    };

    const bool ok = post(qid, qgen, label,
        [this, delayedIdx, delayedGen, tok = FiredDelayedToken(this, delayedIdx, delayedGen)]() mutable noexcept
        {
            runDelayedByIndex(delayedIdx, delayedGen);
            tok.released = true;
        });

    if (!ok)
        discardFiredDelayed(delayedIdx, delayedGen);
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

    const uint32_t tmId = ds.tmTimerId.load(std::memory_order_acquire);
    if (tmId != 0)
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

void ControlScheduler::destroyRefState(ProcessQueueRefState* state) noexcept
{
    if (!state)
        return;

    RefTimerNode* head = state->timerHead.exchange(nullptr, std::memory_order_acq_rel);

    while (head)
    {
        RefTimerNode* next = head->next.load(std::memory_order_relaxed);

        if (head->active.load(std::memory_order_acquire))
            cancelDelayed(head->handle);

        delete head;
        head = next;
    }
}

void ControlScheduler::drainProcessQueue(ProcessQueueId id, uint32_t gen) noexcept
{
    if (id >= maxProcessQueues)
        return;

    ProcessQueueSlot& slot = pqSlots[id];

    if (!slot.active.load(std::memory_order_acquire))
        return;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    ProcessQueueState& st = slot.state;

    st.draining.store(true, std::memory_order_release);
    st.drainThreadMarker.store(&gcontrolEngineTlsMarker, std::memory_order_relaxed);
    st.waitCv.notify_all();

    uint16_t rr = 0;

    while (true)
    {
        const bool run = !st.closed.load(std::memory_order_acquire);

        while (true)
        {
            const uint16_t n = st.subCount;
            bool consumed = false;

            for (uint16_t idx = rr; idx < n; ++idx)
            {
                if (st.sub[idx].tryConsumeOne(run))
                {
                    rr = (idx + 1 == n) ? 0 : static_cast<uint16_t>(idx + 1);
                    consumed = true;
                    break;
                }
            }

            if (!consumed)
            {
                for (uint16_t idx = 0; idx < rr; ++idx)
                {
                    if (st.sub[idx].tryConsumeOne(run))
                    {
                        rr = (idx + 1 == n) ? 0 : static_cast<uint16_t>(idx + 1);
                        consumed = true;
                        break;
                    }
                }
            }

            if (!consumed)
                break;

            if (st.closed.load(std::memory_order_acquire))
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

        bool expected = false;
        if (!st.scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            break;
    }

    st.drainThreadMarker.store(nullptr, std::memory_order_release);
    st.draining.store(false, std::memory_order_release);
    st.waitCv.notify_all();

    if (st.deferDestroy.load(std::memory_order_acquire))
        finalizeDestroy(id, gen);
}

void ControlScheduler::destroyProcessQueue(ProcessQueueId id, uint32_t gen) noexcept
{
    if (id >= maxProcessQueues)
        return;

    ProcessQueueSlot& slot = pqSlots[id];

    if (!slot.active.load(std::memory_order_acquire))
        return;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    ProcessQueueState& st = slot.state;

    st.closed.store(true, std::memory_order_release);

    if (st.drainThreadMarker.load(std::memory_order_acquire) == &gcontrolEngineTlsMarker)
    {
        st.deferDestroy.store(true, std::memory_order_release);
        return;
    }

    if (st.hasAnyPending())
    {
        const bool was = st.scheduled.exchange(true, std::memory_order_acq_rel);
        if (!was)
        {
            uint32_t spins = 0;
            while (!pool.enqueue([this, id, gen] { drainProcessQueue(id, gen); }))
            {
                if (++spins <= 64)
                    _mm_pause();
                else
                    std::this_thread::yield();
            }
        }
    }

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
    if (id >= maxProcessQueues)
        return;

    ProcessQueueSlot& slot = pqSlots[id];

    if (!slot.active.load(std::memory_order_acquire))
        return;

    if (slot.generation.load(std::memory_order_acquire) != gen)
        return;

    ProcessQueueState& st = slot.state;

    for (uint16_t i = 0; i < st.subCount; ++i)
        st.sub[i].reset();

    st.resetConfig();

    slot.generation.fetch_add(1, std::memory_order_release);
    slot.active.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> g(pqAllocMtx);
        pqFreeIds.push_back(id);
    }
}

} // namespace core
