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
#include <immintrin.h>
#include <ThreadPool.hpp>
#include <TimeManager.h>

class ProcessQueueRef;
class ProcessQueue;

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


    // External pool (engine does not shut it down)
    explicit ControlScheduler(ThreadPool& externalPool, TimeManager& tmgr, size_t maxQueues = 4096, size_t maxDelayedTimers = 4096);

    ~ControlScheduler();

    ControlScheduler(const ControlScheduler&) = delete;
    ControlScheduler& operator=(const ControlScheduler&) = delete;

    TimeManager& timers() noexcept { return timeManager; }

    ProcessQueue create(uint32_t capacity = 4096, std::initializer_list<SubQueueConfig> labeled = {});

    bool cancelDelayed(uint32_t timerId) noexcept;

private:
    struct SubQueue
    {
        struct Slot
        {
            std::atomic<uint64_t> seq;
            ThreadPool::Task task;
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
                uintptr_t dif = static_cast<uintptr_t>(seq) - static_cast<uintptr_t>(pos);

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

        // Single comsumer (drainer). Id run == false => discard without executing.
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

            // subqueues
            labelCount = 0;
            subCount = 0;
        };

        bool hasAnyPending() const noexcept
        {
            for (uint16_t i = 0; i < subCount; ++i)
                if (sub[i].hasItem()) return true;
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
                if (v < label) lo = static_cast<uint16_t>(mid + 1);
                else hi = mid;
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
        ThreadPool::Task task;

        ProcessQueueId qid = 0;
        uint32_t gen = 0;
        bool hasLabel = false;
        Label label{0};

        bool inUse = false;
        uint32_t nextFree = kInvalidIndex;
    };

    struct DelayedToken
    {
        ControlScheduler* engine = nullptr;
        uint32_t delayedIndex = kInvalidIndex;
        bool released = false;

        ~DelayedToken() noexcept
        {
            if (engine && !released && delayedIndex != kInvalidIndex)
            {
                engine->discardDelayedByIndex(delayedIndex);
            }
        }
    };

private:
    friend class ProcessQueue;

    template <typename F>
    bool post(ProcessQueueId id, uint32_t gen, std::optional<Label> label, F&& fn) noexcept;

    template <typename F>
    uint32_t schedule(ProcessQueueId id, uint32_t gen, std::optional<Label> label, std::chrono::steady_clock::time_point, F&& fn) noexcept;

    // Draining
    void drainProcessQueue(ProcessQueueId id, uint32_t gen) noexcept;

    // Destruction
    void destroyProcessQueue(ProcessQueueId id, uint32_t gen) noexcept;
    void finalizeDestroy(ProcessQueueId id, uint32_t gen) noexcept;

    void onTimerFired(uint32_t timerId) noexcept;

    // Delayed
    uint32_t allocDelayedSlot() noexcept;
    void freeDelayedSlot(uint32_t idx) noexcept;

    void runDelayedByIndex(uint32_t idx) noexcept;
    void discardDelayedByIndex(uint32_t idx) noexcept;

private:
    std::atomic<bool> stopping{false};

    ThreadPool& pool;
    TimeManager& timeManager;

    // Processing queue table
    const size_t maxProcessQueues;
    ProcessQueueSlot* pqSlots = nullptr;

    std::mutex pqAllocMtx;
    std::vector<ProcessQueueId> pqFreeIds;

    // delayed table
    const size_t maxDelayedTimers;
    DelayedSlot* delayedSlots = nullptr;

    std::mutex delayedMtx;
    std::unordered_map<uint32_t, uint32_t> timerToDelayedIndex;
    uint32_t delayedFreeHead = kInvalidIndex;

    // timer callback
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
uint32_t ControlScheduler::schedule(ProcessQueueId id, uint32_t gen, std::optional<Label> label, std::chrono::steady_clock::time_point expiration, F&& fn) noexcept
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

    uint32_t delayedIdx = kInvalidIndex;
    uint32_t timerId = 0;

    {
        std::lock_guard<std::mutex> g(delayedMtx);

        delayedIdx = allocDelayedSlot();
        if (delayedIdx == kInvalidIndex)
            return 0;

        DelayedSlot& ds = delayedSlots[delayedIdx];
        ds.qid = id;
        ds.gen = gen;
        ds.hasLabel = label.has_value();
        ds.label = label.value_or(Label{0});

        timerId = timeManager.addTimer(expiration, [this](uint32_t firedId) noexcept {
            onTimerFired(firedId);
        });

        // If timerId is somehow 0, clean up and fail.
        if (timerId == 0)
        {
            freeDelayedSlot(delayedIdx);
            return 0;
        }

        ds.task.set(
            [fn = std::forward<F>(fn), timerId]() mutable {
                fn(timerId);
            }
        );

        timerToDelayedIndex.emplace(timerId, delayedIdx);
    }

    return timerId;
}

class ProcessQueueRef
{
    friend class ControlScheduler;
    friend class ProcessQueue;
    
public:
    template <typename F>
    bool post(F&& fn) const noexcept
    {
        return engine.post(id, gen, std::nullopt, std::forward<F>(fn));
    }

    template <typename F>
    bool post(ControlScheduler::Label label, F&& fn) const noexcept
    {
        return engine.post(id, gen, label, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t postAfter(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        return engine.schedule(id, gen, std::nullopt, expiration, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t postAfter(ControlScheduler::Label label, std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        return engine.schedule(id, gen, label, expiration, std::forward<F>(fn));
    }

    bool cancel(uint32_t timerId) noexcept
    {
        return engine.cancelDelayed(timerId);
    }

    ControlScheduler::ProcessQueueId getId() const noexcept { return id; }
    uint32_t generation() const noexcept { return gen; }

private:
    ProcessQueueRef(ControlScheduler& e, ControlScheduler::ProcessQueueId id, uint32_t gen) noexcept
        : engine(e), id(id), gen(gen)
    {}

    ControlScheduler& engine;
    ControlScheduler::ProcessQueueId id = 0;
    uint32_t gen = 0;
};

class ProcessQueue
{
    friend class ControlScheduler;

public:
    ~ProcessQueue() { reset(); }

    ProcessQueue(const ProcessQueue&) = delete;
    ProcessQueue& operator=(const ProcessQueue&) = delete;

    ProcessQueue(ProcessQueue&& o) noexcept
        : engine(o.engine), id(o.id), gen(o.gen)
    {
        o.id = 0;
        o.gen = 0;
    }

    template <typename F>
    bool post(F&& fn) const noexcept
    {
        return engine.post(id, gen, std::nullopt, std::forward<F>(fn));
    }

    template <typename F>
    bool post(ControlScheduler::Label label, F&& fn) const noexcept
    {
        return engine.post(id, gen, label, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t schedule(std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        return engine.schedule(id, gen, std::nullopt, expiration, std::forward<F>(fn));
    }

    template <typename F>
    uint32_t schedule(ControlScheduler::Label label, std::chrono::steady_clock::time_point expiration, F&& fn) const noexcept
    {
        return engine.schedule(id, gen, label, expiration, std::forward<F>(fn));
    }

    bool cancel(uint32_t timerId) noexcept
    {
        return engine.cancelDelayed(timerId);
    }

    ProcessQueueRef ref() const noexcept
    {
        return ProcessQueueRef(engine, id, gen);
    }

    void reset() noexcept
    {
        engine.destroyProcessQueue(id, gen);
        id = 0;
        gen = 0;
    }

    ControlScheduler::ProcessQueueId getId() const noexcept { return id; }
    uint32_t generation() const noexcept { return gen; }

private:
    ProcessQueue(ControlScheduler& e, ControlScheduler::ProcessQueueId id, uint32_t gen) noexcept
        : engine(e), id(id), gen(gen)
    {}

    ControlScheduler& engine;
    ControlScheduler::ProcessQueueId id = 0;
    uint32_t gen = 0;
};

#endif // CONTROL_ENGINE_H
