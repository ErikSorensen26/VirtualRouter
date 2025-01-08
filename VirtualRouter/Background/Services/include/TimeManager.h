// TimeManager.h

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>

class TimeManager {
public:
    // Get the singleton instance
    static TimeManager& getInstance() {
        static TimeManager instance;
        return instance;
    }

    // Adds a timer task to the manager and returns a unique timer ID
    uint32_t addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void()> callback);

    // Cancels a timer based on its ID
    void cancelTimer(uint32_t timerId);

    // Stops the timer manager and its thread
    void stopTimer();

    // Delete copy constructor and assignment operator to prevent multiple instances
    TimeManager(const TimeManager&) = delete;
    void operator=(const TimeManager&) = delete;

private:
    // Internal structure for timer tasks
    struct TimerEntry {
        std::chrono::steady_clock::time_point expirationTime;
        std::function<void()> callback;
    };

    // Private constructor and destructor for Singleton
    TimeManager();
    ~TimeManager();

    void Run(); // The worker function for the timer thread

    std::mutex mutex;
    std::condition_variable cv;
    std::map<uint32_t, TimerEntry> timers;
    uint32_t currentTimerId;
    std::thread timerThread;
    bool stop;
};

#endif // TIMERMANAGER_H
