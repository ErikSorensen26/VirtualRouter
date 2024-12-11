// TimerManager.cpp
#include <TimeManager.h>

TimeManager::TimeManager() : currentTimerId(0), stop(false)
{
    timerThread = std::thread(&TimeManager::Run, this);
}

TimeManager::~TimeManager() 
{
    stopTimer();
}

int TimeManager::addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(mutex);
    int timerId = currentTimerId++;
    timers[timerId] = TimerEntry{ expirationTime, callback};
    cv.notify_one(); // Wake up the timer thread
    return timerId;
}

void TimeManager::cancelTimer(int timerId) 
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = timers.find(timerId);
        if (it != timers.end())
        {
            timers.erase(it);
            cv.notify_one();
        }
    }
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
            cv.wait(lock);
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
                // Execute the callback
                auto callback = nextTimerIt->second.callback;
                int timerId = nextTimerIt->first;
                timers.erase(nextTimerIt);
                lock.unlock();
                callback();
                lock.lock();
            }
            else
            {
                cv.wait_until(lock, nextTimerIt->second.expirationTime);
            }
        }
    }
}
