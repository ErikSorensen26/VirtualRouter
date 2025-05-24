// TimeManager.h

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <ThreadPool.hpp>

class TimeManager 
{
public:
    TimeManager(ThreadPool& pool);
    ~TimeManager();

    // Adds a timer task to the manager and returns a unique timer ID
    uint32_t addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void()> callback);

    // Recurring timer
    uint32_t addRecurringTimer(std::chrono::milliseconds interval, std::function<void()> callback);

    // Cancels a timer based on its ID
    bool cancelTimer(uint32_t timerId);

    // Updates the interval time on a timer
    void updateInterval(uint32_t timerId, std::chrono::milliseconds newInterval);

    // Stops the timer manager and its thread
    void stopTimer();

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    struct TimerData
    {
        uint32_t id;
        std::function<void()> callback;
        std::chrono::milliseconds interval; // 0 for one-shot
    };

    void Run(); // The worker function for the timer thread
    void scheduleCallback(const TimerData& data);

    std::atomic<uint32_t> nextTimerId;
    std::atomic<bool> stop;
    std::mutex mutex;
    std::condition_variable cv;
    
    std::multimap<TimePoint, TimerData> timers;
    std::unordered_map<uint32_t, std::multimap<TimePoint, TimerData>::iterator> timerIndex;
    std::unordered_map<uint32_t, bool> executing; // For save cancel sync
    std::unordered_map<uint32_t, std::chrono::milliseconds> dynamicIntervals;
    std::unordered_map<uint32_t, std::thread::id> executingThreads;
    std::unordered_map<uint32_t, std::atomic<bool>> cancelFlags;
    std::condition_variable timerDoneCV;
    std::thread timerThread;
    ThreadPool& threadPool;
};

#endif // TIMERMANAGER_H
