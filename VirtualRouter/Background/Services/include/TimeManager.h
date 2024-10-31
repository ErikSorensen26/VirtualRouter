// TimerManager.h
#ifndef TIMERMANAGER_H
#define TIMERMANAGER_H

#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <iostream>


class TimeManager {
public:
    // Get the singleton instance
    static TimeManager& getInstance() {
        static TimeManager instance;
        return instance;
    }

    // Adds a timer task to the manager and returns a unique timer ID
    int AddTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void()> callback);

    // Cancels a timer based on its ID
    void CancelTimer(int timerId);

    // Stops the timer manager and its thread
    void Stop();

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
    std::map<int, TimerEntry> timers;
    int currentTimerId;
    std::thread timerThread;
    bool stop;
};

#endif // TIMERMANAGER_H
