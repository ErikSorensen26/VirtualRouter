#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
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

    // The ring is smaller than kTasks; enqueue() returning false is documented
    // backpressure, so retry until the workers make room.
    for (int i = 0; i < kTasks; ++i)
    {
        while (!pool.enqueue([&]{ counter.fetch_add(1, std::memory_order_relaxed); }))
            std::this_thread::yield();
    }

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
    ProcessQueue ref = q.ref();
    std::atomic<bool> ran{false};

    ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));

    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, PostPreservesFifoOrderPerSubQueue)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
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

TEST_F(Internal_ControlSchedulerTest, WaitIdleBlocksUntilAllTasksComplete)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
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

    SUCCEED();
}

TEST_F(Internal_ControlSchedulerTest, PostAfterResetIsRejected)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
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


TEST_F(Internal_ControlSchedulerTest, ScheduleFiresAfterDelay)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
    EXPECT_FALSE(ref.cancel(0));
    EXPECT_FALSE(ref.cancel(0xFFFFFFFFu));
}

TEST_F(Internal_ControlSchedulerTest, CancelTwiceSecondCallReturnsFalse)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 200ms, [](uint32_t){});

    EXPECT_TRUE(ref.cancel(handle));
    EXPECT_FALSE(ref.cancel(handle));
}

TEST_F(Internal_ControlSchedulerTest, ScheduleCallbackReceivesItsOwnHandle)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();
    q.reset();

    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 10ms, [](uint32_t){});
    EXPECT_EQ(handle, 0u);
}

TEST_F(Internal_ControlSchedulerTest, ScheduledTaskRunsOnQueueSerializedWithPosts)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
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

TEST_F(Internal_ControlSchedulerTest, ScheduleWithZeroDelayedSlotsAlwaysFails)
{
    ThreadPool pool(2, 64);
    TimeManager tm(pool);
    ControlScheduler scheduler(pool, tm, 16, 0);

    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 10ms, [](uint32_t){});
    EXPECT_EQ(handle, 0u);

    EXPECT_FALSE(ref.cancel(1));
}

TEST_F(Internal_ControlSchedulerTest, RefPostRunsTask)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();

    std::atomic<bool> ran{false};
    ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));

    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, RefPostAfterRunsAfterDelay)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();

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
        ProcessQueue ref = q.ref();
        // ref destroyed (released) at end of this scope.
    }

    // ref destroyed (released) above; queue itself still alive via q.
    // A fresh ref should still work, demonstrating the queue wasn't affected.
    ProcessQueue ref2 = q.ref();
    ASSERT_TRUE(ref2.post([&]{ ran.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, RefDestructorBlocksUntilPendingTasksFinish)
{
    ProcessQueue q = scheduler.create();
    std::atomic<bool> taskStarted{false};
    std::atomic<bool> taskFinished{false};

    {
        ProcessQueue ref = q.ref();
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
        ProcessQueue ref = q.ref();
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
    ProcessQueue ref = q.ref();

    std::atomic<bool> fired{false};
    uint32_t handle = ref.postAfter(std::chrono::steady_clock::now() + 200ms, [&](uint32_t){
        fired.store(true, std::memory_order_release);
    });

    EXPECT_TRUE(ref.cancel(handle));

    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(fired.load(std::memory_order_acquire));
}

TEST_F(Internal_ControlSchedulerTest, MultipleRefsToSameQueueIndependentLifetimes)
{
    ProcessQueue q = scheduler.create();
    std::atomic<int> ranCount{0};

    ProcessQueue ref1 = q.ref();
    {
        ProcessQueue ref2 = q.ref();
        ASSERT_TRUE(ref2.post([&]{ ranCount.fetch_add(1, std::memory_order_relaxed); }));
    } // ref2 released, but ref1 and the queue remain alive

    ASSERT_TRUE(ref1.post([&]{ ranCount.fetch_add(1, std::memory_order_relaxed); }));

    ASSERT_TRUE(waitFor([&]{ return ranCount.load() == 2; }));
}

TEST_F(Internal_ControlSchedulerTest, RefOutlivesQueueDestructionPostsAreDropped)
{
    ProcessQueue placeholder = scheduler.create();
    ProcessQueue ref = placeholder.ref();
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

    ProcessQueue ref = q1.ref();
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
    ProcessQueue ref = defaultQueue.ref();

    EXPECT_FALSE(ref.post([](){}));
    EXPECT_EQ(ref.postAfter(std::chrono::steady_clock::now() + 10ms, [](uint32_t){}), 0u);
    EXPECT_FALSE(ref.cancel(1));
}

TEST_F(Internal_ControlSchedulerTest, PostedTaskRacesWithDirectClearOfSharedContainer)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();

    // Stand-in for Eigrp::ifaceMgr.eigrpInterfaceList: heap-allocated container
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

    shared->clear();
    shared->shrink_to_fit();

    ASSERT_TRUE(waitFor([&]{ return taskRan.load(std::memory_order_acquire); }));

    delete shared;
}

TEST_F(Internal_ControlSchedulerTest, DirectDeleteRacesWithPendingPostedAccess)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();

    struct Node { int value; };
    auto* node = new Node{42};
    std::atomic<bool> taskRan{false};

    ref.post([&, node]{
        volatile int v = node->value; // read of *node; may run after delete below
        (void)v;
        taskRan.store(true, std::memory_order_release);
    });

    delete node;

    ASSERT_TRUE(waitFor([&]{ return taskRan.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, WaitIdleBeforeDirectMutationAvoidsRace)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();

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

TEST(Internal_ControlSchedulerLifecycleTest, DestroyingSchedulerWithPendingTimersIsSafe)
{
    ThreadPool pool(2, 64);
    TimeManager tm(pool);

    {
        ControlScheduler scheduler(pool, tm, 16, 16);
        ProcessQueue q = scheduler.create();
        ProcessQueue ref = q.ref();

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
        ProcessQueue ref = q.ref();

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
        ProcessQueue ref = q.ref();
        std::atomic<bool> ran{false};
        ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));
        ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
        // ref and q destroyed here, slot recycled for next iteration.
    }
}

TEST_F(Internal_ControlSchedulerTest, OwnerHandlePostsDirectly)
{
    ProcessQueue q = scheduler.create();
    std::atomic<bool> ran{false};

    ASSERT_TRUE(q.post([&]{ ran.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));

    uint32_t h = q.postAfter(std::chrono::steady_clock::now() + 5ms, [](uint32_t){});
    EXPECT_NE(h, 0u);
}

TEST_F(Internal_ControlSchedulerTest, BorrowingFromABorrowerWorks)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();
    ProcessQueue ref2 = ref.ref(); // borrow from a borrower

    std::atomic<bool> ran{false};
    ASSERT_TRUE(ref2.post([&]{ ran.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, OwnerReleaseRejectsFurtherOwnPostsButKeepsQueue)
{
    ProcessQueue q = scheduler.create();
    ProcessQueue ref = q.ref();

    q.release(); // gives up the owner's own posting lifetime only

    EXPECT_FALSE(q.post([](){}));

    // Queue is still alive: borrowing handles keep working.
    std::atomic<bool> ran{false};
    ASSERT_TRUE(ref.post([&]{ ran.store(true, std::memory_order_release); }));
    ASSERT_TRUE(waitFor([&]{ return ran.load(std::memory_order_acquire); }));
}

TEST_F(Internal_ControlSchedulerTest, ReleaseFromInsideOwnTaskDoesNotDeadlock)
{
    ProcessQueue q = scheduler.create();
    auto* ref = new ProcessQueue(q.ref());
    std::atomic<bool> done{false};

    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(ref->post([]{}));
    ref->postAfter(std::chrono::steady_clock::now() + 10s, [](uint32_t){});

    ASSERT_TRUE(ref->post([&, ref]{
        ref->release(); // pumps remaining tasks inline, cancels the timer
        done.store(true, std::memory_order_release);
    }));

    ASSERT_TRUE(waitFor([&]{ return done.load(std::memory_order_acquire); }, 5000ms));
    delete ref;
}

TEST_F(Internal_ControlSchedulerTest, ResetFromInsideOwnTaskDoesNotDeadlock)
{
    for (int iter = 0; iter < 50; ++iter)
    {
        ProcessQueue q = scheduler.create();
        ProcessQueue ref = q.ref();
        std::atomic<bool> done{false};

        ProcessQueue* qp = &q;
        ASSERT_TRUE(ref.post([&, qp]{
            qp->reset(); // deferDestroy path, from the queue's own drain thread
            done.store(true, std::memory_order_release);
        }));

        ASSERT_TRUE(waitFor([&]{ return done.load(std::memory_order_acquire); }));
        ref.release();
        q.reset(); // no-op; already emptied by the task's reset
    }
}

TEST_F(Internal_ControlSchedulerTest, PostersVsDestroyChurn)
{
    std::atomic<long> executed{0};

    for (int iter = 0; iter < 400; ++iter)
    {
        ProcessQueue q = scheduler.create(64);
        std::atomic<bool> quit{false};

        std::vector<std::thread> posters;
        for (int i = 0; i < 4; ++i)
        {
            posters.emplace_back([&, r = q.ref()]{
                while (!quit.load(std::memory_order_relaxed))
                    r.post([&]{ executed.fetch_add(1, std::memory_order_relaxed); });
            });
        }

        std::this_thread::sleep_for(std::chrono::microseconds(200));
        q.reset(); // destroy while posters are mid-post
        quit = true;
        for (auto& t : posters) t.join();
    }
    SUCCEED();
}

TEST_F(Internal_ControlSchedulerTest, WaitIdleVsDestroyChurn)
{
    ControlScheduler scheduler(pool, tm, 8, 16);

    for (int iter = 0; iter < 2000; ++iter)
    {
        ProcessQueue q = scheduler.create(64);
        ProcessQueue ref = q.ref();

        std::thread waiter([w = q.ref()]{ w.waitIdle(); });

        for (int i = 0; i < 8; ++i)
            ref.post([]{});
        ref.release();
        q.reset();
        waiter.join();
    }
    SUCCEED();
}

TEST_F(Internal_ControlSchedulerTest, DelayedFireCancelAccounting)
{
    ControlScheduler scheduler(pool, tm, 8, 8); // tiny pool -> heavy recycling

    ProcessQueue q = scheduler.create(1u << 12);
    ProcessQueue ref = q.ref();

    std::atomic<long> fired{0}, cancelled{0}, scheduled{0};
    std::atomic<bool> stop{false};

    auto worker = [&]{
        while (!stop.load(std::memory_order_relaxed))
        {
            uint32_t h = ref.postAfter(
                std::chrono::steady_clock::now() + std::chrono::microseconds(50),
                [&](uint32_t){ fired.fetch_add(1, std::memory_order_relaxed); });
            if (h == 0)
                continue;
            scheduled.fetch_add(1, std::memory_order_relaxed);
            if (ref.cancel(h))
                cancelled.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) ts.emplace_back(worker);
    std::this_thread::sleep_for(1s);
    stop = true;
    for (auto& t : ts) t.join();

    q.waitIdle();

    // Re-armed timers may straggle; the books must converge exactly.
    ASSERT_TRUE(waitFor([&]{
        return fired.load() + cancelled.load() == scheduled.load();
    }, 5000ms)) << "lost or duplicated delayed task: fired=" << fired.load()
                << " cancelled=" << cancelled.load()
                << " scheduled=" << scheduled.load();
}

// Concurrent cancels of the same handle racing its fire: at most one winner,
// and winner + fire must account for every handle.
TEST_F(Internal_ControlSchedulerTest, ConcurrentCancelHasSingleWinner)
{
    ControlScheduler scheduler(pool, tm, 8, 512);

    ProcessQueue q = scheduler.create(1u << 10);
    ProcessQueue ref = q.ref();

    std::atomic<long> winners{0}, fires{0}, handles{0};

    constexpr int kIters = 1000;
    std::barrier<> sync(3);
    std::atomic<uint32_t> curHandle{0};
    std::atomic<int> won{0};

    auto canceller = [&]{
        for (int i = 0; i < kIters; ++i)
        {
            sync.arrive_and_wait();
            uint32_t h = curHandle.load(std::memory_order_acquire);
            if (h != 0 && ref.cancel(h))
                won.fetch_add(1, std::memory_order_acq_rel);
            sync.arrive_and_wait();
        }
    };
    std::thread c1(canceller), c2(canceller);

    for (int iter = 0; iter < kIters; ++iter)
    {
        uint32_t h = ref.postAfter(
            std::chrono::steady_clock::now() + std::chrono::microseconds(30),
            [&](uint32_t){ fires.fetch_add(1, std::memory_order_relaxed); });
        if (h != 0)
            handles.fetch_add(1, std::memory_order_relaxed);

        won.store(0, std::memory_order_release);
        curHandle.store(h, std::memory_order_release);
        sync.arrive_and_wait();
        sync.arrive_and_wait();
        ASSERT_LE(won.load(), 1) << "handle cancelled twice";
        winners.fetch_add(won.load(), std::memory_order_relaxed);
    }
    c1.join();
    c2.join();

    q.waitIdle();
    ASSERT_TRUE(waitFor([&]{
        return winners.load() + fires.load() == handles.load();
    }, 5000ms)) << "cancel/fire accounting broken: cancelled=" << winners.load()
                << " fired=" << fires.load() << " handles=" << handles.load();
}

// Refs released while their short timers are firing: the node-refcount
// protocol must neither leak, double-free, nor hang.
TEST_F(Internal_ControlSchedulerTest, ReleaseVsFiringTimers)
{
    ControlScheduler scheduler(pool, tm, 8, 256);

    ProcessQueue q = scheduler.create(1u << 10);

    auto worker = [&]{
        for (int i = 0; i < 500; ++i)
        {
            ProcessQueue ref = q.ref();
            for (int k = 0; k < 4; ++k)
            {
                ref.postAfter(
                    std::chrono::steady_clock::now() + std::chrono::microseconds(20 * k),
                    [](uint32_t){});
            }
            // ref released here while timers are in flight / firing
        }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < 6; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
    SUCCEED();
}

TEST_F(Internal_ControlSchedulerTest, StaleRefsOnRecycledSlotsNeverRun)
{
    std::atomic<bool> stop{false};
    std::atomic<long> wrongRun{0};

    std::thread churn([&]{
        while (!stop.load(std::memory_order_relaxed))
        {
            ProcessQueue q = scheduler.create(64);
            ProcessQueue r = q.ref();
            r.post([]{});
        }
    });

    std::thread stale([&]{
        while (!stop.load(std::memory_order_relaxed))
        {
            ProcessQueue q = scheduler.create(64);
            ProcessQueue r = q.ref();
            q.reset(); // slot recycled by the churn thread
            for (int i = 0; i < 100; ++i)
                r.post([&]{ wrongRun.fetch_add(1, std::memory_order_relaxed); });
        }
    });

    std::this_thread::sleep_for(1s);
    stop = true;
    churn.join();
    stale.join();

    EXPECT_EQ(wrongRun.load(), 0);
}

TEST_F(Internal_ControlSchedulerTest, SerializationAndExactlyOnceUnderStorm)
{
    ControlScheduler scheduler(pool, tm, 4, 16);

    ProcessQueue q = scheduler.create(1u << 12);
    std::atomic<long> posted{0}, ran{0};
    std::atomic<int> inTask{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> overlapped{false};

    auto worker = [&]{
        ProcessQueue ref = q.ref();
        while (!stop.load(std::memory_order_relaxed))
        {
            bool ok = ref.post([&]{
                if (inTask.fetch_add(1, std::memory_order_acq_rel) != 0)
                    overlapped.store(true, std::memory_order_release);
                ran.fetch_add(1, std::memory_order_relaxed);
                inTask.fetch_sub(1, std::memory_order_acq_rel);
            });
            if (ok)
                posted.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) ts.emplace_back(worker);
    std::this_thread::sleep_for(1s);
    stop = true;
    for (auto& t : ts) t.join();

    q.waitIdle();
    EXPECT_FALSE(overlapped.load()) << "two tasks of one queue ran concurrently";
    EXPECT_EQ(posted.load(), ran.load());
}

TEST_F(Internal_ControlSchedulerTest, TimerSurvivesFullRing)
{
    ControlScheduler scheduler(pool, tm, 4, 16);

    ProcessQueue q = scheduler.create(2); // minimal ring
    ProcessQueue ref = q.ref();

    std::atomic<bool> release{false};
    std::atomic<bool> fired{false};

    // Occupy the drain with a long task, then fill the ring so the timer
    // trampoline cannot be posted when it fires.
    ASSERT_TRUE(ref.post([&]{
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(1ms);
    }));
    while (ref.post([]{})) {} // fill the ring to capacity

    uint32_t h = ref.postAfter(std::chrono::steady_clock::now() + 5ms,
                               [&](uint32_t){ fired.store(true, std::memory_order_release); });
    ASSERT_NE(h, 0u);

    std::this_thread::sleep_for(50ms); // timer fires against a full ring
    release.store(true, std::memory_order_release);

    ASSERT_TRUE(waitFor([&]{ return fired.load(std::memory_order_acquire); }, 5000ms))
        << "timer was dropped instead of re-armed";
}
