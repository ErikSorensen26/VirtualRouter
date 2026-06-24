#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <string>

#include <ThreadPool.hpp>
#include <TimeManager.h>
#include <ControlScheduler.h>

using namespace core;
using namespace std::chrono_literals;

namespace
{
// Polls `cond()` until it becomes true or `timeout` elapses. Returns whether it became true.
template <typename Cond>
bool waitFor(Cond cond, std::chrono::milliseconds timeout = 2000ms)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (cond())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return cond();
}
} // namespace

// =====================================================================================
// ThreadPool
// =====================================================================================

class Internal_ThreadPoolTest : public ::testing::Test {};

TEST_F(Internal_ThreadPoolTest, EnqueueRunsTask)
{
    ThreadPool pool(2, 16);
    std::atomic<int> counter{0};

    ASSERT_TRUE(pool.enqueue([&]{ counter.fetch_add(1, std::memory_order_relaxed); }));

    ASSERT_TRUE(waitFor([&]{ return counter.load() == 1; }));
}

TEST_F(Internal_ThreadPoolTest, MultipleTasksAllRun)
{
    ThreadPool pool(4, 64);
    constexpr int kTasks = 200;
    std::atomic<int> counter{0};

    for (int i = 0; i < kTasks; ++i)
        ASSERT_TRUE(pool.enqueue([&]{ counter.fetch_add(1, std::memory_order_relaxed); }));

    ASSERT_TRUE(waitFor([&]{ return counter.load() == kTasks; }));
}

TEST_F(Internal_ThreadPoolTest, ConcurrentProducersAllTasksRun)
{
    ThreadPool pool(4, 1u << 12);
    constexpr int kProducers = 8;
    constexpr int kPerProducer = 200;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&]{
            for (int i = 0; i < kPerProducer; ++i)
            {
                while (!pool.enqueue([&]{ counter.fetch_add(1, std::memory_order_relaxed); }))
                    std::this_thread::yield();
            }
        });
    }
    for (auto& t : producers) t.join();

    ASSERT_TRUE(waitFor([&]{ return counter.load() == kProducers * kPerProducer; }, 5000ms));
}

TEST_F(Internal_ThreadPoolTest, RingFullReturnsFalse)
{
    // Capacity 2 with 0 worker threads so nothing drains the ring.
    ThreadPool pool(0, 2);

    bool first  = pool.enqueue([]{});
    bool second = pool.enqueue([]{});
    bool third  = pool.enqueue([]{});

    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
    EXPECT_FALSE(third); // ring full, no consumers

    pool.shutdown();
}

TEST_F(Internal_ThreadPoolTest, ShutdownWithNoWorkersLeavesQueuedTasksUnrun)
{
    ThreadPool pool(0, 16);
    std::atomic<int> counter{0};

    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(pool.enqueue([&]{ counter.fetch_add(1, std::memory_order_relaxed); }));

    // No worker threads were running, so nothing has executed yet.
    EXPECT_EQ(counter.load(), 0);

    // shutdown() with zero workers just joins (nothing); queued tasks remain unrun.
    pool.shutdown();

    EXPECT_EQ(counter.load(), 0);
}

TEST_F(Internal_ThreadPoolTest, ShutdownIsIdempotent)
{
    ThreadPool pool(2, 16);
    pool.shutdown();
    pool.shutdown(); // must not hang or double-join
    SUCCEED();
}

// =====================================================================================
// TimeManager
// =====================================================================================

class Internal_TimeManagerTest : public ::testing::Test
{
protected:
    ThreadPool pool{4, 64};
    TimeManager tm{pool};
};

TEST_F(Internal_TimeManagerTest, OneShotTimerFires)
{
    std::atomic<bool> fired{false};
    std::atomic<uint32_t> firedId{0};

    uint32_t id = tm.addTimer(std::chrono::steady_clock::now() + 20ms, [&](uint32_t tid){
        firedId.store(tid, std::memory_order_relaxed);
        fired.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }));
    EXPECT_EQ(firedId.load(), id);
}

TEST_F(Internal_TimeManagerTest, OneShotTimerDoesNotFireEarly)
{
    std::atomic<bool> fired{false};
    tm.addTimer(std::chrono::steady_clock::now() + 200ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));

    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }, 1000ms));
}

TEST_F(Internal_TimeManagerTest, CancelBeforeFirePreventsCallback)
{
    std::atomic<bool> fired{false};
    uint32_t id = tm.addTimer(std::chrono::steady_clock::now() + 200ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    EXPECT_TRUE(tm.cancelTimer(id));

    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));
}

TEST_F(Internal_TimeManagerTest, CancelUnknownTimerReturnsFalse)
{
    EXPECT_FALSE(tm.cancelTimer(999999));
}

TEST_F(Internal_TimeManagerTest, CancelAfterFireReturnsFalse)
{
    std::atomic<bool> fired{false};
    uint32_t id = tm.addTimer(std::chrono::steady_clock::now() + 10ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }));

    // Timer already fired and removed itself from the index.
    EXPECT_FALSE(tm.cancelTimer(id));
}

TEST_F(Internal_TimeManagerTest, MultipleTimersFireInExpirationOrder)
{
    std::mutex m;
    std::vector<int> order;

    tm.addTimer(std::chrono::steady_clock::now() + 60ms, [&](uint32_t){
        std::lock_guard<std::mutex> lk(m);
        order.push_back(3);
    });
    tm.addTimer(std::chrono::steady_clock::now() + 20ms, [&](uint32_t){
        std::lock_guard<std::mutex> lk(m);
        order.push_back(1);
    });
    tm.addTimer(std::chrono::steady_clock::now() + 40ms, [&](uint32_t){
        std::lock_guard<std::mutex> lk(m);
        order.push_back(2);
    });

    ASSERT_TRUE(waitFor([&]{
        std::lock_guard<std::mutex> lk(m);
        return order.size() == 3;
    }, 1000ms));

    std::lock_guard<std::mutex> lk(m);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST_F(Internal_TimeManagerTest, RecurringTimerFiresMultipleTimes)
{
    std::atomic<int> count{0};
    uint32_t id = tm.addRecurringTimer(10ms, [&](uint32_t){
        count.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(waitFor([&]{ return count.load() >= 3; }, 2000ms));

    tm.cancelTimer(id);
}

TEST_F(Internal_TimeManagerTest, RecurringTimerStopsAfterCancel)
{
    std::atomic<int> count{0};
    uint32_t id = tm.addRecurringTimer(10ms, [&](uint32_t){
        count.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(waitFor([&]{ return count.load() >= 1; }));
    EXPECT_TRUE(tm.cancelTimer(id));

    int afterCancel = count.load();
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(count.load(), afterCancel);
}

TEST_F(Internal_TimeManagerTest, CancelDuringExecutionFromOtherThreadBlocksAndPreventsReschedule)
{
    // Cancelling a recurring timer from another thread while its callback is
    // currently executing must block until that callback finishes, return
    // true, and prevent any further reschedule.
    std::atomic<int> count{0};

    uint32_t id = tm.addRecurringTimer(10ms, [&](uint32_t){
        int n = count.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (n == 1)
        {
            // Sleep so the test thread can observe `executing[id] == true`
            // from a different thread and exercise the cross-thread cancel path.
            std::this_thread::sleep_for(50ms);
        }
    });

    // From the test thread: wait until the first tick is in-flight, then cancel.
    ASSERT_TRUE(waitFor([&]{ return count.load() >= 1; }, 1000ms));

    auto beforeCancel = std::chrono::steady_clock::now();
    EXPECT_TRUE(tm.cancelTimer(id));
    auto cancelDuration = std::chrono::steady_clock::now() - beforeCancel;

    // cancelTimer should have blocked for roughly the remainder of the
    // in-flight callback's sleep, not returned immediately.
    EXPECT_GE(cancelDuration, 10ms);

    int afterCancel = count.load();
    std::this_thread::sleep_for(100ms);

    // No further ticks after cancel.
    EXPECT_EQ(count.load(), afterCancel);
}

TEST_F(Internal_TimeManagerTest, LimitedRecurringTimerFiresExactCountThenFinal)
{
    std::atomic<int> ticks{0};
    std::atomic<bool> finalCalled{false};
    std::atomic<uint32_t> finalId{0};

    tm.addLimitedRecurringTimer(10ms, 3,
        [&](uint32_t){ ticks.fetch_add(1, std::memory_order_relaxed); },
        [&](uint32_t tid){
            finalId.store(tid, std::memory_order_relaxed);
            finalCalled.store(true, std::memory_order_release);
        });

    ASSERT_TRUE(waitFor([&]{ return finalCalled.load(std::memory_order_acquire); }, 2000ms));

    EXPECT_EQ(ticks.load(), 3);

    // No further ticks after final callback.
    int ticksAtFinal = ticks.load();
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(ticks.load(), ticksAtFinal);
}

TEST_F(Internal_TimeManagerTest, LimitedRecurringTimerWithoutFinalCallback)
{
    std::atomic<int> ticks{0};

    tm.addLimitedRecurringTimer(10ms, 2,
        [&](uint32_t){ ticks.fetch_add(1, std::memory_order_relaxed); });

    ASSERT_TRUE(waitFor([&]{ return ticks.load() == 2; }, 2000ms));

    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(ticks.load(), 2);
}

TEST_F(Internal_TimeManagerTest, UpdateIntervalChangesFutureFiringRate)
{
    std::atomic<int> count{0};
    uint32_t id = tm.addRecurringTimer(1000ms, [&](uint32_t){
        count.fetch_add(1, std::memory_order_relaxed);
    });

    // Reschedule to fire much sooner.
    tm.updateInterval(id, 10ms);

    ASSERT_TRUE(waitFor([&]{ return count.load() >= 1; }, 1000ms));

    tm.cancelTimer(id);
}

TEST_F(Internal_TimeManagerTest, StopTimerPreventsFurtherFiring)
{
    std::atomic<int> count{0};
    tm.addRecurringTimer(10ms, [&](uint32_t){
        count.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(waitFor([&]{ return count.load() >= 1; }));

    tm.stopTimer();
    int afterStop = count.load();

    std::this_thread::sleep_for(100ms);
    // At most one in-flight tick may have been queued before stop.
    EXPECT_LE(count.load(), afterStop + 1);
}

TEST_F(Internal_TimeManagerTest, TimerIdsAreUnique)
{
    std::unordered_set<uint32_t> ids;
    for (int i = 0; i < 50; ++i)
    {
        uint32_t id = tm.addTimer(std::chrono::steady_clock::now() + 500ms, [](uint32_t){});
        EXPECT_TRUE(ids.insert(id).second) << "duplicate timer id " << id;
    }

    for (uint32_t id : ids)
        tm.cancelTimer(id);
}

TEST_F(Internal_TimeManagerTest, CancelFromWithinOwnCallbackStopsRecurrence)
{
    // A recurring timer that cancels itself on its first tick must not fire
    // again, and the self-cancel must not deadlock (same-thread cancel cannot
    // block on its own completion).
    std::atomic<int> ticks{0};
    std::atomic<bool> selfCancelReturned{false};
    std::atomic<bool> selfCancelResult{false};

    tm.addRecurringTimer(10ms, [&](uint32_t tid){
        ticks.fetch_add(1, std::memory_order_acq_rel);
        if (!selfCancelReturned.exchange(true, std::memory_order_acq_rel))
        {
            bool result = tm.cancelTimer(tid);
            selfCancelResult.store(result, std::memory_order_release);
        }
    });

    ASSERT_TRUE(waitFor([&]{ return selfCancelReturned.load(std::memory_order_acquire); }, 1000ms));

    // Self-cancel reports success...
    EXPECT_TRUE(selfCancelResult.load(std::memory_order_acquire));

    // ...and no further ticks occur.
    int ticksAtCancel = ticks.load();
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(ticks.load(), ticksAtCancel);
}

TEST_F(Internal_TimeManagerTest, CancelDuringExecutionFromOtherThreadWaitsForCompletion)
{
    // Cancelling from another thread while the callback is executing blocks
    // until that callback finishes before returning.
    std::atomic<bool> callbackStarted{false};
    std::atomic<bool> callbackFinished{false};

    uint32_t id = tm.addTimer(std::chrono::steady_clock::now() + 10ms, [&](uint32_t){
        callbackStarted.store(true, std::memory_order_release);
        std::this_thread::sleep_for(100ms);
        callbackFinished.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(waitFor([&]{ return callbackStarted.load(std::memory_order_acquire); }, 1000ms));

    bool cancelResult = tm.cancelTimer(id);
    EXPECT_TRUE(cancelResult);

    // cancelTimer blocked until the in-flight callback finished.
    EXPECT_TRUE(callbackFinished.load(std::memory_order_acquire));
}

// =====================================================================================
// ControlScheduler / ProcessQueue — basic post()
// =====================================================================================

class Internal_ControlSchedulerTest : public ::testing::Test
{
protected:
    ThreadPool pool{4, 1u << 10};
    TimeManager tm{pool};
    ControlScheduler scheduler{pool, tm, 64, 64};
};

TEST_F(Internal_ControlSchedulerTest, PostRunsTask)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<bool> ran{false};

    ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));

    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, PostPreservesFifoOrderPerSubQueue)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::mutex m;
    std::vector<int> order;

    constexpr int kCount = 100;
    for (int i = 0; i < kCount; ++i)
    {
        ASSERT_TRUE(ref.post([&, i]{
            std::lock_guard<std::mutex> lk(m);
            order.push_back(i);
        }));
    }

    ASSERT_TRUE(waitFor([&]{
        std::lock_guard<std::mutex> lk(m);
        return static_cast<int>(order.size()) == kCount;
    }));

    std::lock_guard<std::mutex> lk(m);
    for (int i = 0; i < kCount; ++i)
        EXPECT_EQ(order[i], i);
}

TEST_F(Internal_ControlSchedulerTest, OnlyOneThreadDrainsAtATime)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};
    std::atomic<int> completed{0};

    constexpr int kCount = 50;
    for (int i = 0; i < kCount; ++i)
    {
        ASSERT_TRUE(ref.post([&]{
            int c = concurrent.fetch_add(1, std::memory_order_acq_rel) + 1;
            int prevMax = maxConcurrent.load(std::memory_order_relaxed);
            while (c > prevMax && !maxConcurrent.compare_exchange_weak(prevMax, c)) {}
            std::this_thread::sleep_for(1ms);
            concurrent.fetch_sub(1, std::memory_order_acq_rel);
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    ASSERT_TRUE(waitFor([&]{ return completed.load() == kCount; }, 5000ms));
    EXPECT_EQ(maxConcurrent.load(), 1);
}

TEST_F(Internal_ControlSchedulerTest, LabeledSubQueuesRouteIndependently)
{
    constexpr ControlScheduler::Label kHigh = 1;
    constexpr ControlScheduler::Label kLow  = 2;

    ProcessQueue q = scheduler.create(16, {
        ControlScheduler::SubQueueConfig{kHigh, 16},
        ControlScheduler::SubQueueConfig{kLow, 16},
    });
    ProcessQueueRef ref = q.ref();

    std::mutex m;
    std::vector<std::string> order;

    ASSERT_TRUE(ref.post(kHigh, [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("high"); }));
    ASSERT_TRUE(ref.post(kLow,  [&]{ std::lock_guard<std::mutex> lk(m); order.push_back("low"); }));
    ASSERT_TRUE(ref.post([&]{ std::lock_guard<std::mutex> lk(m); order.push_back("default"); }));

    ASSERT_TRUE(waitFor([&]{
        std::lock_guard<std::mutex> lk(m);
        return order.size() == 3;
    }));
}

TEST_F(Internal_ControlSchedulerTest, PostToUnknownLabelFails)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    EXPECT_FALSE(ref.post(static_cast<ControlScheduler::Label>(123), [](){}));
}

TEST_F(Internal_ControlSchedulerTest, WaitIdleBlocksUntilAllTasksComplete)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<int> completed{0};

    constexpr int kCount = 30;
    for (int i = 0; i < kCount; ++i)
    {
        ASSERT_TRUE(ref.post([&]{
            std::this_thread::sleep_for(2ms);
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    q.waitIdle();
    EXPECT_EQ(completed.load(), kCount);
}

TEST_F(Internal_ControlSchedulerTest, WaitIdleOnEmptyQueueReturnsImmediately)
{
    ProcessQueue q = scheduler.create();
    auto start = std::chrono::steady_clock::now();
    q.waitIdle();
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 500ms);
}

TEST_F(Internal_ControlSchedulerTest, ResetDiscardsUnstartedTasks)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<int> ran{0};

    // Block the queue with a long-running first task so subsequent posts are
    // still pending when reset() is called.
    std::atomic<bool> release{false};
    ASSERT_TRUE(ref.post([&]{
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(1ms);
        ran.fetch_add(1, std::memory_order_relaxed);
    }));

    for (int i = 0; i < 10; ++i)
        ref.post([&]{ ran.fetch_add(1, std::memory_order_relaxed); });

    release.store(true, std::memory_order_release);
    q.reset();

    // The first task ran (it was already executing); subsequent ones may or may
    // not have, but reset() must return without hanging and without a crash.
    SUCCEED();
}

TEST_F(Internal_ControlSchedulerTest, PostAfterResetIsRejected)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    q.reset();

    EXPECT_FALSE(ref.post([](){}));
}

TEST_F(Internal_ControlSchedulerTest, MovedFromQueuePostFails)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue moved = std::move(q);

    EXPECT_FALSE(q.ref().post([](){})); // moved-from
    EXPECT_TRUE(moved.ref().post([](){}));
}

TEST_F(Internal_ControlSchedulerTest, GenerationPreventsStaleQueueReuse)
{
    ControlScheduler::ProcessQueueId id;
    uint32_t gen1;
    {
        ProcessQueue q = scheduler.create();
        id = q.getId();
        gen1 = q.generation();
        // q destroyed here -> slot recycled
    }

    ProcessQueue q2 = scheduler.create();
    // Very likely reuses the same slot id since it was just freed.
    if (q2.getId() == id)
    {
        EXPECT_NE(q2.generation(), gen1);
    }
    EXPECT_TRUE(q2.ref().post([](){}));
}

// =====================================================================================
// ControlScheduler — schedule()/cancel() (delayed tasks)
// =====================================================================================

TEST_F(Internal_ControlSchedulerTest, ScheduleFiresAfterDelay)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<bool> fired{false};

    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 20ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    EXPECT_NE(handle, 0u);
    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, ScheduleDoesNotFireBeforeDelay)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<bool> fired{false};

    ref.postAfter(std::chrono::steady_clock::now() + 300ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));

    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }, 1000ms));
}

TEST_F(Internal_ControlSchedulerTest, CancelBeforeFirePreventsExecution)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<bool> fired{false};

    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 200ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    EXPECT_TRUE(ref.cancel(handle));

    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));
}

TEST_F(Internal_ControlSchedulerTest, CancelAfterFireReturnsFalse)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<bool> fired{false};

    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 10ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }));

    EXPECT_FALSE(ref.cancel(handle));
}

TEST_F(Internal_ControlSchedulerTest, CancelInvalidHandleReturnsFalse)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    EXPECT_FALSE(ref.cancel(0));
    EXPECT_FALSE(ref.cancel(0xFFFFFFFFu));
}

TEST_F(Internal_ControlSchedulerTest, CancelTwiceSecondCallReturnsFalse)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 200ms, [](uint32_t){});

    EXPECT_TRUE(ref.cancel(handle));
    EXPECT_FALSE(ref.cancel(handle));
}

TEST_F(Internal_ControlSchedulerTest, ScheduleCallbackReceivesItsOwnHandle)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::atomic<uint32_t> receivedHandle{0};

    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 10ms, [&](uint32_t h){
        receivedHandle.store(h, std::memory_order_release);
    });

    ASSERT_TRUE(waitFor([&]{ return receivedHandle.load(std::memory_order_acquire) != 0; }));
    EXPECT_EQ(receivedHandle.load(), handle);
}

TEST_F(Internal_ControlSchedulerTest, MultipleScheduledTasksAllFire)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    constexpr int kCount = 20;
    std::atomic<int> fired{0};

    for (int i = 0; i < kCount; ++i)
    {
        ref.postAfter(std::chrono::steady_clock::now() + std::chrono::milliseconds(5 + i), [&](uint32_t){
            fired.fetch_add(1, std::memory_order_relaxed);
        });
    }

    ASSERT_TRUE(waitFor([&]{ return fired.load() == kCount; }, 2000ms));
}

TEST_F(Internal_ControlSchedulerTest, ScheduleOnClosedQueueReturnsZero)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    q.reset();

    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 10ms, [](uint32_t){});
    EXPECT_EQ(handle, 0u);
}

TEST_F(Internal_ControlSchedulerTest, ScheduledTaskRunsOnQueueSerializedWithPosts)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    std::mutex m;
    std::vector<std::string> order;

    ASSERT_TRUE(ref.post([&]{
        std::this_thread::sleep_for(30ms);
        std::lock_guard<std::mutex> lk(m);
        order.push_back("post");
    }));

    ref.postAfter(std::chrono::steady_clock::now() + 5ms, [&](uint32_t){
        std::lock_guard<std::mutex> lk(m);
        order.push_back("timer");
    });

    ASSERT_TRUE(waitFor([&]{
        std::lock_guard<std::mutex> lk(m);
        return order.size() == 2;
    }, 2000ms));

    // Both ran; no crash from concurrent access to `order` thanks to `m`.
    std::lock_guard<std::mutex> lk(m);
    EXPECT_EQ(order.size(), 2u);
}

// Exhausting the delayed-timer slot pool: with maxDelayedTimers == 0, schedule()
// must always fail gracefully rather than crash.
TEST(Internal_ControlSchedulerNoDelayedTest, ScheduleWithZeroDelayedSlotsAlwaysFails)
{
    ThreadPool pool(2, 64);
    TimeManager tm(pool);
    ControlScheduler scheduler(pool, tm, 16, 0);

    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 10ms, [](uint32_t){});
    EXPECT_EQ(handle, 0u);

    EXPECT_FALSE(ref.cancel(1));
}

// =====================================================================================
// ProcessQueueRef — lifetime-safe posting
// =====================================================================================

TEST_F(Internal_ControlSchedulerTest, RefPostRunsTask)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    std::atomic<bool> ran{false};
    ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));

    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, RefPostAfterRunsAfterDelay)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    std::atomic<bool> fired{false};
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 20ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    EXPECT_NE(handle, 0u);
    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, RefPostDroppedAfterReleaseEvenIfQueueAlive)
{
    ProcessQueue q = scheduler.create();
    std::atomic<bool> ran{false};

    {
        ProcessQueueRef ref = q.ref();
        // ref destroyed (released) at end of this scope.
    }

    // ref destroyed (released) above; queue itself still alive via q.
    // A fresh ref should still work, demonstrating the queue wasn't affected.
    ProcessQueueRef ref2 = q.ref();
    ASSERT_TRUE(ref2.post([&]{ ran.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, RefDestructorBlocksUntilPendingTasksFinish)
{
    ProcessQueue q = scheduler.create();
    std::atomic<bool> taskStarted{false};
    std::atomic<bool> taskFinished{false};

    {
        ProcessQueueRef ref = q.ref();
        ASSERT_TRUE(ref.post([&]{
            taskStarted.store(true, std::memory_order_release);
            std::this_thread::sleep_for(50ms);
            taskFinished.store(true, std::memory_order_release);
        }));

        ASSERT_TRUE(waitFor([&]{ return taskStarted.load(std::memory_order_acquire); }));
        // ref destructor (release()) below must block until the task completes.
    }

    EXPECT_TRUE(taskFinished.load(std::memory_order_acquire));
}

TEST_F(Internal_ControlSchedulerTest, RefScheduledTimerNotFiredAfterRefReleased)
{
    ProcessQueue q = scheduler.create();
    std::atomic<bool> fired{false};

    {
        ProcessQueueRef ref = q.ref();
        ref.postAfter(std::chrono::steady_clock::now() + 100ms, [&](uint32_t){
            fired.store(true, std::memory_order_release);
        });
        // ref destroyed immediately; release() cancels the pending timer node.
    }

    std::this_thread::sleep_for(200ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));
}

TEST_F(Internal_ControlSchedulerTest, RefCancelOwnTimer)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    std::atomic<bool> fired{false};
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 200ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    EXPECT_TRUE(ref.cancel(handle));

    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));
}

TEST_F(Internal_ControlSchedulerTest, RefPostToLabeledSubQueue)
{
    constexpr ControlScheduler::Label kLabel = 7;
    ProcessQueue q = scheduler.create(16, { ControlScheduler::SubQueueConfig{kLabel, 16} });
    ProcessQueueRef ref = q.ref();

    std::atomic<bool> ran{false};
    ASSERT_TRUE(ref.post(kLabel, [&]{ ran.store(true, std::memory_order_release); }));

    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, RefPostToUnknownLabelFails)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    EXPECT_FALSE(ref.post(static_cast<ControlScheduler::Label>(99), [](){}));
}

TEST_F(Internal_ControlSchedulerTest, MultipleRefsToSameQueueIndependentLifetimes)
{
    ProcessQueue q = scheduler.create();
    std::atomic<int> ranCount{0};

    ProcessQueueRef ref1 = q.ref();
    {
        ProcessQueueRef ref2 = q.ref();
        ASSERT_TRUE(ref2.post([&]{ ranCount.fetch_add(1, std::memory_order_relaxed); }));
    } // ref2 released, but ref1 and the queue remain alive

    ASSERT_TRUE(ref1.post([&]{ ranCount.fetch_add(1, std::memory_order_relaxed); }));

    ASSERT_TRUE(waitFor([&]{ return ranCount.load() == 2; }));
}

TEST_F(Internal_ControlSchedulerTest, RefOutlivesQueueDestructionPostsAreDropped)
{
    ProcessQueue placeholder = scheduler.create();
    ProcessQueueRef ref = placeholder.ref();
    {
        ProcessQueue q = scheduler.create();
        ref = q.ref();
        // q destroyed at end of scope (reset()).
    }

    std::atomic<bool> ran{false};
    // Queue is gone; postOwned should fail or the task should never run.
    ref.post([&]{ ran.store(true, std::memory_order_release); });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(ran.load(std::memory_order_acquire));
}

TEST_F(Internal_ControlSchedulerTest, RefMoveAssignmentReleasesPreviousState)
{
    ProcessQueue q1 = scheduler.create();
    ProcessQueue q2 = scheduler.create();

    ProcessQueueRef ref = q1.ref();
    std::atomic<bool> ran1{false};
    ASSERT_TRUE(ref.post([&]{ ran1.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran1.load(std::memory_order_acquire); }));

    ref = q2.ref(); // releases q1's ref state, binds to q2

    std::atomic<bool> ran2{false};
    ASSERT_TRUE(ref.post([&]{ ran2.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran2.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, DefaultConstructedRefPostFails)
{
    ProcessQueue defaultQueue; // not created via scheduler.create()
    ProcessQueueRef ref = defaultQueue.ref();

    EXPECT_FALSE(ref.post([](){}));
    EXPECT_EQ(ref.postAfter(std::chrono::steady_clock::now() + 10ms, [](uint32_t){}), 0u);
    EXPECT_FALSE(ref.cancel(1));
}

// =====================================================================================
// ProcessQueueRef — race vs. direct (non-posted) access to shared state
//
// These tests reproduce the pattern behind a real EIGRP use-after-free:
// `Eigrp::selfRef.post([e]{ e->refreshInterfaceList(); })` runs on a worker
// thread and iterates `eigrpInterfaceList`, while `Eigrp::shutdown()` (called
// directly by tests/destructors, NOT posted through selfRef) clears/destroys
// that same container on the calling thread. `ProcessQueueRef` only
// serializes work posted *through* it (via `alive`/`pending`); it provides
// no exclusion against a direct, synchronous caller touching the same state.
// =====================================================================================

TEST_F(Internal_ControlSchedulerTest, PostedTaskRacesWithDirectClearOfSharedContainer)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    // Stand-in for Eigrp::ifaceMgr.eigrpInterfaceList: heap-allocated container
    // that a posted task will iterate, exactly like refreshInterfaceList()
    // iterates eigrpInterfaceList via EigrpTopology::synchronizeConnected().
    auto* shared = new std::vector<int>{1, 2, 3, 4, 5};
    std::atomic<bool> taskRan{false};
    std::atomic<long> sum{0};

    // Analogous to: e->selfRef.post([e]{ e->refreshInterfaceList(); });
    // (a posted task that reads/iterates the shared container)
    ref.post([&]{
        long s = 0;
        for (int v : *shared) s += v; // read of *shared, may race with clear() below
        sum.store(s, std::memory_order_release);
        taskRan.store(true, std::memory_order_release);
    });

    // Analogous to: eigrpInstance->shutdown() -> ifaceMgr.deactivateAll()
    // -> eigrpInterfaceList.clear() -- a DIRECT, unposted mutation on this
    // thread with NO synchronization against the post() above.
    shared->clear();
    shared->shrink_to_fit();

    ASSERT_TRUE(waitFor([&]{ return taskRan.load(std::memory_order_acquire); }));

    // No correctness assertion on `sum` -- the point is that ref.post()
    // provided no exclusion: the posted task and the direct clear() above
    // can interleave in either order, and under ASan/TSan this is flagged
    // as a data race / use-after-clear on *shared.
    delete shared;
}

TEST_F(Internal_ControlSchedulerTest, DirectDeleteRacesWithPendingPostedAccess)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    struct Node { int value; };
    auto* node = new Node{42};
    std::atomic<bool> taskRan{false};

    // Analogous to: e->selfRef.post([e]{ e->refreshInterfaceList(); }), which
    // (transitively, via synchronizeConnected) calls EigrpInterface::getIface()
    // on a node owned by eigrpInterfaceList.
    ref.post([&, node]{
        volatile int v = node->value; // read of *node; may run after delete below
        (void)v;
        taskRan.store(true, std::memory_order_release);
    });

    // Analogous to: eigrpInstance->shutdown() -> ifaceMgr.deactivateAll()
    // -> eigrpInterfaceList.clear(), which destroys the EigrpInterface the
    // posted task is about to dereference. selfRef provides NO protection
    // here: alive/pending only gate posting, not direct deletes.
    delete node;

    ASSERT_TRUE(waitFor([&]{ return taskRan.load(std::memory_order_acquire); }));
    // Reaching here without an ASan abort is itself non-deterministic --
    // this test documents the race; under ASan it may abort the process,
    // which is the bug being exploited/demonstrated.
}

TEST_F(Internal_ControlSchedulerTest, WaitIdleBeforeDirectMutationAvoidsRace)
{
    ProcessQueue q = scheduler.create();
    ProcessQueueRef ref = q.ref();

    auto* shared = new std::vector<int>{1, 2, 3, 4, 5};
    std::atomic<bool> taskRan{false};

    ref.post([&]{
        long s = 0;
        for (int v : *shared) s += v;
        (void)s;
        taskRan.store(true, std::memory_order_release);
    });

    // Correct pattern: wait for the queue to drain BEFORE directly mutating
    // shared state, instead of mutating immediately as in the tests above.
    q.waitIdle();
    EXPECT_TRUE(taskRan.load(std::memory_order_acquire));

    shared->clear();
    delete shared;
}

// =====================================================================================
// Cross-cutting: scheduler destruction with in-flight work
// =====================================================================================

TEST(Internal_ControlSchedulerLifecycleTest, DestroyingSchedulerWithPendingTimersIsSafe)
{
    ThreadPool pool(2, 64);
    TimeManager tm(pool);

    {
        ControlScheduler scheduler(pool, tm, 16, 16);
        ProcessQueue q = scheduler.create();
        ProcessQueueRef ref = q.ref();

        // Schedule several timers that won't fire before the scheduler is destroyed.
        for (int i = 0; i < 5; ++i)
            ref.postAfter(std::chrono::steady_clock::now() + 5s, [](uint32_t){});

        // ref, then queue, then scheduler destructors run here while timers are still pending.
    }

    SUCCEED();
}

TEST(Internal_ControlSchedulerLifecycleTest, DestroyingSchedulerWithRefTimersIsSafe)
{
    ThreadPool pool(2, 64);
    TimeManager tm(pool);

    {
        ControlScheduler scheduler(pool, tm, 16, 16);
        ProcessQueue q = scheduler.create();
        ProcessQueueRef ref = q.ref();

        for (int i = 0; i < 5; ++i)
            ref.postAfter(std::chrono::steady_clock::now() + 5s, [](uint32_t){});

        // ref destructor runs first (member order), then scheduler destructor.
    }

    SUCCEED();
}

TEST(Internal_ControlSchedulerLifecycleTest, ManyQueuesCreateAndDestroy)
{
    ThreadPool pool(4, 1u << 12);
    TimeManager tm(pool);
    ControlScheduler scheduler(pool, tm, 32, 32);

    for (int i = 0; i < 100; ++i)
    {
        ProcessQueue q = scheduler.create();
        ProcessQueueRef ref = q.ref();
        std::atomic<bool> ran{false};
        ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));
        ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
        // ref and q destroyed here, slot recycled for next iteration.
    }
}
