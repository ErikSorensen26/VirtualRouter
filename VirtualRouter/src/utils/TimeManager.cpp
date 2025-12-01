// TimerManager.cpp
#include <TimeManager.h>

TimeManager::TimeManager(ThreadPool& pool)
    : nextTimerId(1), stop(false), threadPool(pool)
{
    timerThread = std::thread(&TimeManager::Run, this);
}

TimeManager::~TimeManager() 
{
    stopTimer();
}

uint32_t TimeManager::addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(mutex);
    uint32_t id = nextTimerId++;
    auto it = timers.emplace(expirationTime, TimerData{ id, std::move(callback), std::chrono::milliseconds(0) });
    timerIndex[id] = it;
    cv.notify_one();
    return id;
}

uint32_t TimeManager::addLimitedRecurringTimer(std::chrono::milliseconds interval, size_t repeatCount, std::function<void()> repeated, std::function<void()> finalCallback)
{
    std::lock_guard<std::mutex> lock(mutex);
    uint32_t id = nextTimerId++;

    // Register repetition limit and initial counter.
    repeatedCounters[id] = 0;
    repeatLimits[id] = repeatCount;
    if (finalCallback)
        finalCallbacks[id] = std::move(finalCallback);

    TimePoint now = std::chrono::steady_clock::now();
    auto it = timers.emplace(now + interval, TimerData{
        id,
        [this, id, repeated = std::move(repeated)]() mutable {
            size_t current = 0;
            size_t limit = 0;
            std::function<void()> final;

            {
                std::lock_guard<std::mutex> lock(mutex);
                current = ++repeatedCounters[id];
                limit = repeatLimits[id];
                if (current == limit)
                {
                    auto finalIt = finalCallbacks.find(id);
                    if (finalIt != finalCallbacks.end())
                    {
                        final = std::move(finalIt->second);
                        finalCallbacks.erase(finalIt);
                    }
                }
            }

            if (current <= limit)
                repeated();

            if (current == limit)
            {
                cancelTimer(id);
                if (final)
                    final();
            }
        },
        interval
    });

    timerIndex[id] = it;
    dynamicIntervals[id] = interval;
    executing[id] = false;
    cv.notify_one();
    return id;
}

uint32_t TimeManager::addRecurringTimer(std::chrono::milliseconds interval, std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(mutex);
    uint32_t id = nextTimerId++;
    TimePoint now = std::chrono::steady_clock::now();
    auto it = timers.emplace(now + interval, TimerData{ id, std::move(callback), interval });
    timerIndex[id] = it;
    cv.notify_one();
    return id;
}

void TimeManager::updateInterval(uint32_t timerId, std::chrono::milliseconds newInterval)
{
    std::lock_guard<std::mutex> lock(mutex);

    std::function<void()> callback;

    // Remove current scheduled instance if exists
    auto it = timerIndex.find(timerId);
    if (it != timerIndex.end())
    {
        callback = it->second->second.callback;
        timers.erase(it->second);
        timerIndex.erase(it);
    }

    // Store new interval
    dynamicIntervals[timerId] = newInterval;

    // Reschedult immediatly
    TimePoint nextTime = std::chrono::steady_clock::now();
    TimerData updated = {
        timerId,
        std::move(callback),
        newInterval
    };
    
    // Insert new timer
    auto newIt = timers.emplace(nextTime + newInterval, updated);
    timerIndex[timerId] = newIt;
    executing[timerId] = false;
    cv.notify_one();
}

bool TimeManager::cancelTimer(uint32_t id)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (stop)
        return false;

    auto it = timerIndex.find(id);
    if (it != timerIndex.end())
    {
        cancelFlags[id].store(true, std::memory_order_relaxed);

        auto nodeIt = it->second;
        if (nodeIt != timers.end())
        {
            auto found = timers.find(nodeIt->first);
            if (found != timers.end())
                timers.erase(found);
        }

        timerIndex.erase(it);
        executing.erase(id);
        executingThreads.erase(id);
        return true;
    }

    if (executing.count(id) && executing[id])
    {
        if (executingThreads.count(id) && executingThreads[id] == std::this_thread::get_id())
            return false;

        //timerDoneCV.wait(lock, [&] { return !executing[id]; });
        executing.erase(id);
        executingThreads.erase(id);
        return true;
    }

    return false;
}

void TimeManager::stopTimer() 
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
    }
    cv.notify_one();
    if (timerThread.joinable())
        timerThread.join();
}

void TimeManager::Run() 
{
    RCU::registerThread();
    std::unique_lock<std::mutex> lock(mutex);
    while (!stop) 
    {
        if (timers.empty())
        {
            cv.wait(lock, [&] { return stop || !timers.empty(); });
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        TimePoint nextTime = timers.begin()->first;
        if (nextTime > now)
        {
            cv.wait_until(lock, nextTime);
            continue;
        }

        std::vector<TimerData> toExecute;
        while (!timers.empty() && timers.begin()->first <= now)
        {
            auto it = timers.begin();
            toExecute.push_back(it->second);
            executing[it->second.id] = true;
            timerIndex.erase(it->second.id);
            timers.erase(it);
        }

        lock.unlock();
        for (auto& timer : toExecute)
        {
            threadPool.enqueue([this, timer]() { runSingleTimer(timer); });
        }
        lock.lock();
    }
    RCU::unregisterThread();
}

void TimeManager::runSingleTimer(const TimerData& timer)
{
    {
        std::unique_lock<std::mutex> lock(mutex);
        executing[timer.id] = true;
        executingThreads[timer.id] = std::this_thread::get_id();
    }
    
    if (timer.callback)
        timer.callback();

    std::chrono::milliseconds rescheduleInterval;
    {
        std::unique_lock<std::mutex> lock(mutex);
        executing[timer.id] = false;
        executingThreads.erase(timer.id);
        timerDoneCV.notify_all();

        auto intervalIt = dynamicIntervals.find(timer.id);
        rescheduleInterval = (intervalIt != dynamicIntervals.end()) ? intervalIt->second : timer.interval;
    }

    if (!stop && rescheduleInterval.count() > 0)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!stop)
        {
            TimePoint nextTime = std::chrono::steady_clock::now() + rescheduleInterval;
            TimerData updated = timer;
            updated.interval = rescheduleInterval;
            auto it = timers.emplace(nextTime, updated);
            timerIndex[timer.id] = it;
            cv.notify_one();
        }
    }
}
