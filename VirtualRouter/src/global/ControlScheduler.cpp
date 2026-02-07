// ControlScheduler.cpp

#include "ControlScheduler.h"
#include <array>

static thread_local int gcontrolEngineTlsMarker = 0;

static void isValidPow2(uint32_t cap) noexcept
{
    assert(cap >= 2 && (cap & (cap - 1)) == 0);
}

void ControlScheduler::SubQueue::init(uint32_t cap)
{
    reset();

    isValidPow2(cap);
    if (cap == 0) std::runtime_error("SubQueue capacity must be non-zero");

    capacity = cap;
    mask = cap - 1;

    slots = new Slot[capacity];

    for (uint32_t i = 0; i < capacity; ++i)
        slots[i].seq.store(static_cast<uint64_t>(i), std::memory_order_relaxed);

    head.store(0, std::memory_order_relaxed);
    head.store(0, std::memory_order_relaxed);
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
                if (run) s->task.run();
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

ControlScheduler::ControlScheduler(ThreadPool& pool, TimeManager& tmgr, size_t maxProcessQueues, size_t maxDelayedTimers)
    : pool(pool),
      timeManager(tmgr),
      maxProcessQueues(maxProcessQueues),
      maxDelayedTimers(maxDelayedTimers)
{
    pqSlots = new ProcessQueueSlot[maxProcessQueues];

    pqFreeIds.reserve(maxProcessQueues);
    for (ProcessQueueId i = 0; i < static_cast<ProcessQueueId>(maxProcessQueues); ++i)
        pqFreeIds.push_back(i);

    delayedSlots = new DelayedSlot[maxDelayedTimers];

    if (maxDelayedTimers > 0)
    {
        delayedFreeHead = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
        {
            delayedSlots[i].inUse = false;
            delayedSlots[i].nextFree = (i + 1 < maxDelayedTimers) ? (i + 1) : kInvalidIndex;
        }
    }
    else
    {
        delayedFreeHead = kInvalidIndex;
    }

    timerToDelayedIndex.reserve(static_cast<size_t>(maxDelayedTimers) * 2);
    timerToDelayedIndex.max_load_factor(0.70f);
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
    }
    
    for (ProcessQueueId i = 0; i < static_cast<ProcessQueueId>(maxProcessQueues); ++i)
    {
        if (pqSlots[i].active.load(std::memory_order_acquire))
        {
            uint32_t gen = pqSlots[i].generation.load(std::memory_order_acquire);
            destroyProcessQueue(i, gen);
        }
    }

    {
        std::lock_guard<std::mutex> g(delayedMtx);
        timerToDelayedIndex.clear();

        for (uint32_t i = 0; i < static_cast<uint32_t>(maxDelayedTimers); ++i)
        {
            if (delayedSlots[i].inUse)
            {
                delayedSlots[i].task.cleanup();
                delayedSlots[i].inUse = false;
                delayedSlots[i].nextFree = delayedFreeHead;
                delayedFreeHead = i;
            }
        }
    }

    delete[] delayedSlots;
    delayedSlots = nullptr;

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

    // Init default SubQueue at index 0
    st.sub[0].init(cap);
    st.subCount = 1;

    std::array<std::pair<uint16_t, uint32_t>, kMaxSubQueues - 1> tmp;
    uint16_t tmpCount = 0;

    for (const auto& cfg : labeled)
        tmp[tmpCount++] = {cfg.label, cfg.capacity};

    std::sort(tmp.begin(), tmp.end(),
        [](const auto& a, const auto& b){ return a.first < b.first; });

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

uint32_t ControlScheduler::allocDelayedSlot() noexcept
{
    if (delayedFreeHead == kInvalidIndex)
        return kInvalidIndex;

    uint32_t idx = delayedFreeHead;
    delayedFreeHead = delayedSlots[idx].nextFree;

    delayedSlots[idx].inUse = true;
    delayedSlots[idx].nextFree = kInvalidIndex;
    return idx;
}

void ControlScheduler::freeDelayedSlot(uint32_t idx) noexcept
{
    if (idx == kInvalidIndex || idx >= maxDelayedTimers)
        return;

    // caller must hold delayedMtx
    delayedSlots[idx].inUse = false;
    delayedSlots[idx].nextFree = delayedFreeHead;
    delayedFreeHead = idx;
}

void ControlScheduler::runDelayedByIndex(uint32_t idx) noexcept
{
    delayedSlots[idx].task.run();
    delayedSlots[idx].task.cleanup();

    {
        std::lock_guard<std::mutex> g(delayedMtx);
        if (idx < maxDelayedTimers && delayedSlots[idx].inUse)
            freeDelayedSlot(idx);
    }
}

void ControlScheduler::discardDelayedByIndex(uint32_t idx) noexcept
{
    std::lock_guard<std::mutex> g(delayedMtx);
    if (idx >= maxDelayedTimers)
        return;

    if (!delayedSlots[idx].inUse)
        return;

    delayedSlots[idx].task.cleanup();
    freeDelayedSlot(idx);
}

void ControlScheduler::onTimerFired(uint32_t timerId) noexcept
{
    timerInFlight.fetch_add(1, std::memory_order_acq_rel);

    struct Guard
    {
        ControlScheduler& self;
        ~Guard() noexcept
        {
            const uint32_t prev = self.timerInFlight.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
            {
                self.timerInFlight.notify_all();
            }
        }
    } guard{*this};

    uint32_t idx = kInvalidIndex;
    ProcessQueueId qid = 0;
    uint32_t gen = 0;
    std::optional<Label> label;

    {
        std::lock_guard<std::mutex> g(delayedMtx);
        auto it = timerToDelayedIndex.find(timerId);
        if (it == timerToDelayedIndex.end())
            return;

        idx = it->second;
        timerToDelayedIndex.erase(it);

        DelayedSlot& ds = delayedSlots[idx];
        qid = ds.qid;
        gen = ds.gen;
        if (ds.hasLabel) label = ds.label;
        else label.reset();
    }

    if (stopping.load(std::memory_order_acquire))
    {
        discardDelayedByIndex(idx);
        return;
    }

    bool ok = post(qid, gen, label, [this, tok = DelayedToken{this, idx, false}]() mutable noexcept {
        runDelayedByIndex(tok.delayedIndex);
        tok.released = true;
    });

    if (!ok)
    {
        discardDelayedByIndex(idx);
    }
}

bool ControlScheduler::cancelDelayed(uint32_t timerId) noexcept
{
    uint32_t idx = kInvalidIndex;

    {
        std::lock_guard<std::mutex> g(delayedMtx);
        auto it = timerToDelayedIndex.find(timerId);
        if (it == timerToDelayedIndex.end())
        {
            return false;
        }

        idx = it->second;
        timerToDelayedIndex.erase(it);

        if (idx < maxDelayedTimers && delayedSlots[idx].inUse)
        {
            delayedSlots[idx].task.cleanup();
            freeDelayedSlot(idx);
        }
    }

    return timeManager.cancelTimer(timerId);
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
    {
        finalizeDestroy(id, gen);
    }
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

    // Close
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

    // Invalidate all ProcessRef copies
    slot.generation.fetch_add(1, std::memory_order_release);
    slot.active.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> g(pqAllocMtx);
        pqFreeIds.push_back(id);
    }
}
