// TimerManager.cpp
#include <TimeManager.h>

TimeManager::TimeManager() : currentTimerId(1), stop(false)
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
    uint32_t timerId = currentTimerId.fetch_add(1);
    timers[timerId] = TimerEntry{ expirationTime, callback};
    cv.notify_one(); // Wake up the timer thread
    return timerId;
}

bool TimeManager::cancelTimer(uint32_t timerId) 
{
    std::unique_lock<std::mutex> lock(mutex);
    // Try to cancel a timer that is still waiting.
    auto it = timers.find(timerId);
    if (it != timers.end())
    {
        timers.erase(it);
        cv.notify_one();
        return true;
    }
    // Otherwise, check if the timer's callback is currently executing.
    while (currentExecutingTimerId.load(std::memory_order_relaxed) == currentTimerId)
    {
        cv.wait(lock);
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
    if (timerThread.joinable()) {
        timerThread.join();
    }
}

void TimeManager::Run() 
{
    std::unique_lock<std::mutex> lock(mutex);
    while (!stop) 
    {
        if (timers.empty()) 
        {
            // Wait indefinitely until a new timer is added or stop is called
            cv.wait(lock, [this](){ return stop || !timers.empty(); });
        } 
        else 
        {
            auto nextTimerIt = std::min_element(timers.begin(), timers.end(),
                [](const auto& a, const auto& b) {
                    return a.second.expirationTime < b.second.expirationTime;
                });

            auto now = std::chrono::steady_clock::now();
            if (nextTimerIt->second.expirationTime <= now) 
            {
                uint32_t timerId = nextTimerIt->first;
                TimerEntry timerEntry = nextTimerIt->second;
                timers.erase(nextTimerIt);
                // Mark this timer as currently executing
                currentExecutingTimerId.store(timerId, std::memory_order_release);
                // Unlock while executing the callback.
                lock.unlock();
                timerEntry.callback();
                lock.lock();
                // Reset curreentExecutingTimerId and notify waiting threads.
                currentExecutingTimerId.store(0, std::memory_order_release);
                cv.notify_all();
            }
            else
            {
                cv.wait_until(lock, nextTimerIt->second.expirationTime);
            }
        }
    }
}
