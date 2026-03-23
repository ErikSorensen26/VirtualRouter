// ControlScheduler.h

#ifndef CONTROL_ENGINE_H
#define CONTROL_ENGINE_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <condition_variable>
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

struct RefTimerNode
{
    std::atomic<RefTimerNode*> next{nullptr};
    std::atomic<bool> active{true};
    uint32_t handle = 0;
};

struct ProcessQueueRefState
{
    std::atomic<bool> alive{true};
    std::atomic<uint32_t> pending{0};
    std::atomic<RefTimerNode*> timerHead{nullptr};
};

inline void releaseRefPending(ProcessQueueRefState* state) noexcept
{
    if (!state)
        return;

    const uint32_t prev = state->pending.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
        state->pending.notify_all();
}

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

class ControlScheduler
{
    static constexpr uint64_t kMaxSubQueues = 8;
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

public:
    using ProcessQueueId = uint32_t;
    using Label = uint16_t;

    struct SubQueueConfig
    {
        Label label;
        uint32_t capacity;
    };

    explicit ControlScheduler(core::ThreadPool& externalPool,
                              core::TimeManager& tmgr,
                              size_t maxQueues = 4096,
                              size_t maxDelayedTimers = 4096);

    ~ControlScheduler();

    ControlScheduler(const ControlScheduler&) = delete;
    ControlScheduler& operator=(const ControlScheduler&) = delete;

    core::TimeManager& timers() noexcept { return timeManager; }

    ProcessQueue create(uint32_t capacity = 4096,
                        std::initializer_list<SubQueueConfig> labeled = {});

    bool cancelDelayed(uint32_t timerId) noexcept;

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
    std::atomic<bool> stopping{false};

    core::ThreadPool& pool;
    core::TimeManager& timeManager;

    const size_t maxProcessQueues;
    ProcessQueueSlot* pqSlots = nullptr;

    std::mutex pqAllocMtx;
    std::vector<ProcessQueueId> pqFreeIds;

    const size_t maxDelayedTimers;
    DelayedSlot* delayedSlots = nullptr;

    std::atomic<uint32_t> delayedFreeHead{kInvalidIndex};

    const uint32_t delayedIndexBits;
    const uint32_t delayedIndexMask;
    const uint32_t delayedGenerationMask;

    std::atomic<uint32_t> timerInFlight{0};
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
        [owner, token = RefPendingToken(owner), fn = std::forward<F>(fn)]() mutable noexcept
        {
            if (owner->alive.load(std::memory_order_acquire))
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
            [owner, token = RefPendingToken(owner), fn = std::forward<F>(fn), publicHandle]() mutable noexcept
            {
                if (owner->alive.load(std::memory_order_acquire))
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

    template <typename F>
    bool post(F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, std::nullopt, state, std::forward<F>(fn));
    }

    template <typename F>
    bool post(ControlScheduler::Label label, F&& fn) const noexcept
    {
        if (!engine || !state)
            return false;

        return engine->postOwned(id, gen, label, state, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t postAfter(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, std::nullopt, state, expiration, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t postAfter(ControlScheduler::Label label,
                       std::chrono::steady_clock::time_point expiration,
                       F&& fn) const noexcept
    {
        if (!engine || !state)
            return 0;

        return engine->schedule(id, gen, label, state, expiration, std::forward<F>(fn));
    }

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

private:
    ControlScheduler* engine = nullptr;
    ControlScheduler::ProcessQueueId id = 0;
    uint32_t gen = 0;
    ProcessQueueRefState* state = nullptr;
};

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

    template <typename F>
    bool post(F&& fn) const noexcept
    {
        if (!engine)
            return false;

        return engine->post(id, gen, std::nullopt, std::forward<F>(fn));
    }

    template <typename F>
    bool post(ControlScheduler::Label label, F&& fn) const noexcept
    {
        if (!engine)
            return false;

        return engine->post(id, gen, label, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t schedule(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        if (!engine)
            return 0;

        return engine->schedule(id, gen, std::nullopt, nullptr, expiration, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t schedule(ControlScheduler::Label label,
                      std::chrono::steady_clock::time_point expiration,
                      F&& fn) const noexcept
    {
        if (!engine)
            return 0;

        return engine->schedule(id, gen, label, nullptr, expiration, std::forward<F>(fn));
    }

    bool cancel(uint32_t timerId) noexcept
    {
        if (!engine)
            return false;

        return engine->cancelDelayed(timerId);
    }

    ProcessQueueRef ref() const noexcept
    {
        if (!engine)
            return ProcessQueueRef();

        return ProcessQueueRef(*engine, id, gen);
    }

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
    ControlScheduler* engine = nullptr;
    ControlScheduler::ProcessQueueId id = 0;
    uint32_t gen = 0;
};

} // namespace core

#endif // CONTROL_ENGINE_H

