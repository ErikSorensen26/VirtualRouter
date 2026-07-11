// TimeManager.cpp

#include <vector>

#include <TimeManager.h>

namespace core
{

TimeManager::TimeManager(ThreadPool& pool)
    : nextTimerId(1), stop(false), threadPool(pool)
{
    timerThread = std::thread(&TimeManager::Run, this);
}

TimeManager::~TimeManager()
{
    stopTimer();
}

uint32_t TimeManager::addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void(uint32_t)> callback)
{
    std::lock_guard<std::mutex> lock(mutex);
    const uint32_t id = nextTimerId++;

    TimerState& st = timers[id];
    st.callback = std::move(callback);
    st.queuePos = queue.emplace(expirationTime, id);
    st.queued = true;

    cv.notify_one();
    return id;
}

uint32_t TimeManager::addLimitedRecurringTimer(std::chrono::milliseconds interval, size_t repeatCount, std::function<void(uint32_t)> repeated, std::function<void(uint32_t)> finalCallback)
{
    std::lock_guard<std::mutex> lock(mutex);
    const uint32_t id = nextTimerId++;

    if (repeatCount == 0)
        return id; // Nothing to fire; ID is valid but the timer is already dead.

    TimerState& st = timers[id];
    st.callback = std::move(repeated);
    st.interval = interval;
    st.repeatLimit = repeatCount;
    st.finalCallback = std::move(finalCallback);
    st.queuePos = queue.emplace(std::chrono::steady_clock::now() + interval, id);
    st.queued = true;

    cv.notify_one();
    return id;
}

uint32_t TimeManager::addRecurringTimer(std::chrono::milliseconds interval, std::function<void(uint32_t)> callback)
{
    std::lock_guard<std::mutex> lock(mutex);
    const uint32_t id = nextTimerId++;

    TimerState& st = timers[id];
    st.callback = std::move(callback);
    st.interval = interval;
    st.queuePos = queue.emplace(std::chrono::steady_clock::now() + interval, id);
    st.queued = true;

    cv.notify_one();
    return id;
}

void TimeManager::updateInterval(uint32_t timerId, std::chrono::milliseconds newInterval)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto it = timers.find(timerId);
    if (it == timers.end())
        return;

    TimerState& st = it->second;
    st.interval = newInterval;

    if (st.queued)
    {
        // Reschedule immediately at the new interval.
        queue.erase(st.queuePos);
        st.queuePos = queue.emplace(std::chrono::steady_clock::now() + newInterval, timerId);
        cv.notify_one();
    }
    // If executing, the new interval is picked up on the post-callback reschedule.
}

bool TimeManager::cancelTimer(uint32_t id)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (stop)
        return false;

    auto it = timers.find(id);
    if (it == timers.end())
        return false;

    TimerState& st = it->second;

    if (st.executing)
    {
        // Prevent the post-callback reschedule; runSingleTimer erases the entry.
        st.cancelled = true;

        // Self-cancel from within the callback cannot block on its own completion.
        if (st.executingThread == std::this_thread::get_id())
            return true;

        timerDoneCV.wait(lock, [&]
        {
            auto cur = timers.find(id);
            return cur == timers.end() || !cur->second.executing;
        });
        return true;
    }

    if (st.queued)
        queue.erase(st.queuePos);

    timers.erase(it);
    return true;
}

void TimeManager::stopTimer()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
    }
    cv.notify_all();
    if (timerThread.joinable())
        timerThread.join();

    // Wait for callbacks already handed to the pool; after this returns no
    // callback can touch this object (or anything scheduled through it).
    uint32_t v = dispatched.load(std::memory_order_acquire);
    while (v != 0)
    {
        dispatched.wait(v, std::memory_order_relaxed);
        v = dispatched.load(std::memory_order_acquire);
    }
}

void TimeManager::Run()
{
    utils::RCU::registerThread();
    std::unique_lock<std::mutex> lock(mutex);
    while (!stop)
    {
        if (queue.empty())
        {
            cv.wait(lock, [&] { return stop.load() || !queue.empty(); });
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const TimePoint nextTime = queue.begin()->first;
        if (nextTime > now)
        {
            cv.wait_until(lock, nextTime);
            continue;
        }

        std::vector<uint32_t> due;
        while (!queue.empty() && queue.begin()->first <= now)
        {
            const uint32_t id = queue.begin()->second;
            queue.erase(queue.begin());

            auto it = timers.find(id);
            if (it == timers.end())
                continue;

            it->second.queued = false;
            it->second.executing = true;
            due.push_back(id);
        }

        lock.unlock();
        for (uint32_t id : due)
        {
            dispatched.fetch_add(1, std::memory_order_acq_rel);
            while (!threadPool.enqueue([this, id]() { runSingleTimer(id); }))
                std::this_thread::yield(); // Pool ring full; it drains as long as the pool outlives us.
        }
        lock.lock();
    }
    utils::RCU::unregisterThread();
}

void TimeManager::runSingleTimer(uint32_t id)
{
    Callback cb;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = timers.find(id);
        if (it != timers.end())
        {
            it->second.executingThread = std::this_thread::get_id();
            cb = it->second.callback; // Copy: the entry may be mutated while the callback runs.
        }
    }

    if (cb)
        cb(id);

    Callback finalCb;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = timers.find(id);
        if (it != timers.end())
        {
            TimerState& st = it->second;
            st.executing = false;
            st.executingThread = {};

            bool dead = st.cancelled || stop || st.interval.count() <= 0;

            if (st.repeatLimit > 0 && !st.cancelled && ++st.repeatCount >= st.repeatLimit)
            {
                dead = true;
                finalCb = std::move(st.finalCallback);
            }

            if (dead)
            {
                timers.erase(it);
            }
            else
            {
                // Reschedule in the same critical section that clears
                // `executing`, so a concurrent cancelTimer() can never miss
                // both the queue entry and the executing flag.
                st.queuePos = queue.emplace(std::chrono::steady_clock::now() + st.interval, id);
                st.queued = true;
                cv.notify_one();
            }

            timerDoneCV.notify_all();
        }
    }

    if (finalCb)
        finalCb(id);

    const uint32_t prev = dispatched.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
        dispatched.notify_all();
}

} // namespace core
