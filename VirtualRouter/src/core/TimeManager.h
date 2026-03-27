/**
 * @file TimeManager.h
 * @brief Deadline-based timer manager that dispatches callbacks through a ThreadPool.
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <atomic>

#include "ThreadPool.hpp"

namespace core
{

/**
 * @brief Deadline-based timer manager that dispatches expired callbacks via a @ref ThreadPool.
 * @ingroup CORE
 *
 * `TimeManager` maintains a sorted set of pending timers keyed by expiration time.
 * A dedicated internal thread (`timerThread`) sleeps until the next deadline, then
 * submits the callback to the shared `ThreadPool` for execution.
 *
 * Supports three timer flavors:
 * - **One-shot**: fires once at an absolute time point.
 * - **Recurring**: fires repeatedly at a fixed interval until cancelled.
 * - **Limited recurring**: fires a fixed number of times, then invokes a final callback.
 *
 * ## Lifecycle & Ownership
 * - Owned by @ref Global; must outlive all users.
 * - `stopTimer()` signals the worker thread and waits for it to exit; called from the destructor.
 *
 * ## Concurrency Model
 * - All internal state is guarded by a single `mutex`.
 * - Callbacks are dispatched to the `ThreadPool`, so they execute on pool threads, not
 *   the timer thread itself.
 * - `cancelTimer()` waits for the callback to complete if it is currently executing,
 *   using `timerDoneCV` to avoid a race between cancel and in-progress execution.
 *
 * @see ControlScheduler
 */
class TimeManager
{
public:
    /**
     * @brief Constructs a TimeManager and starts the internal timer thread.
     * @ingroup CORE
     * @param pool  ThreadPool to which expired timer callbacks are submitted.
     */
    TimeManager(ThreadPool& pool);

    /**
     * @brief Stops the timer thread and cancels all pending timers.
     *
     * Waits for the worker thread to exit before returning.  Any currently
     * executing callbacks are allowed to finish naturally.
     */
    ~TimeManager();

    /**
     * @brief Registers a one-shot timer that fires at an absolute time point.
     *
     * @param expirationTime  Absolute `steady_clock` time point at which to fire.
     * @param callback        Callback invoked with the timer's ID as its argument.
     * @return Unique timer ID; pass to `cancelTimer()` to abort before firing.
     */
    uint32_t addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void(uint32_t)> callback);

    /**
     * @brief Registers a recurring timer that fires `repeatCount` times then calls a final callback.
     *
     * @param interval       Interval between firings.
     * @param repeatCount    Number of times `repeated` is invoked before `finalCallback`.
     * @param repeated       Callback invoked on each interval tick; receives the timer ID.
     * @param finalCallback  Optional callback invoked after the last tick; receives the timer ID.
     * @return Unique timer ID.
     */
    uint32_t addLimitedRecurringTimer(std::chrono::milliseconds interval, size_t repeatCount, std::function<void(uint32_t)> repeated, std::function<void(uint32_t)> finalCallback = nullptr);

    /**
     * @brief Registers a recurring timer that fires indefinitely until cancelled.
     *
     * @param interval  Interval between firings.
     * @param callback  Callback invoked on each tick; receives the timer ID.
     * @return Unique timer ID.
     */
    uint32_t addRecurringTimer(std::chrono::milliseconds interval, std::function<void(uint32_t)> callback);

    /**
     * @brief Cancels a timer by its ID.
     *
     * If the timer is currently executing, this call blocks until the callback returns.
     *
     * @param timerId  ID returned by one of the `add*Timer` methods.
     * @return `true` if the timer was found and cancelled; `false` if already fired or unknown.
     */
    bool cancelTimer(uint32_t timerId);

    /**
     * @brief Updates the firing interval of a recurring timer.
     *
     * Takes effect on the next tick.  The timer is not reset to fire immediately.
     *
     * @param timerId     ID of a recurring timer.
     * @param newInterval New interval to apply from the next firing onward.
     */
    void updateInterval(uint32_t timerId, std::chrono::milliseconds newInterval);

    /**
     * @brief Signals the timer thread to stop and waits for it to exit.
     *
     * Called automatically by the destructor.  May be called explicitly to drain
     * before destroying dependent objects.
     */
    void stopTimer();

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    /**
     * @brief Internal record for a registered timer.
     */
    struct TimerData
    {
        uint32_t id;                              ///< Unique timer identifier assigned at registration.
        std::function<void(uint32_t)> callback;   ///< User-supplied callback; receives `id` as argument.
        std::chrono::milliseconds interval;        ///< Repeat interval; `0` for one-shot timers.
    };

    void Run(); ///< Worker loop: sleeps until the next deadline and dispatches expired timers.
    void runSingleTimer(const TimerData& timer); ///< Fires a single one-shot timer callback via the pool.
    void scheduleCallback(const TimerData& data); ///< Reinserts a recurring timer after firing.

    std::atomic<uint32_t> nextTimerId; ///< Monotonically increasing timer ID counter.
    std::atomic<bool> stop;            ///< Signals the worker thread to exit.
    std::mutex mutex;                  ///< Guards all internal timer state.
    std::condition_variable cv;        ///< Wakes the worker when a new timer is added or stop is set.

    std::multimap<TimePoint, TimerData> timers; ///< Pending timers sorted by expiration.
    std::unordered_map<uint32_t, std::multimap<TimePoint, TimerData>::iterator> timerIndex; ///< Fast O(1) lookup by timer ID into the multimap.
    std::unordered_map<uint32_t, bool> executing; ///< Tracks which timers are currently executing callbacks.
    std::unordered_map<uint32_t, std::chrono::milliseconds> dynamicIntervals; ///< Pending interval overrides for recurring timers.
    std::unordered_map<uint32_t, std::thread::id> executingThreads; ///< Which pool thread is running each active callback.
    std::unordered_map<uint32_t, std::atomic<bool>> cancelFlags; ///< Per-timer cancellation flag checked before callback execution.
    std::unordered_map<uint32_t, size_t> repeatedCounters; ///< Current tick count for limited-recurring timers.
    std::unordered_map<uint32_t, size_t> repeatLimits;     ///< Maximum tick count for limited-recurring timers.
    std::unordered_map<uint32_t, std::function<void(uint32_t)>> finalCallbacks; ///< Final callback for limited-recurring timers.
    std::condition_variable timerDoneCV; ///< Notified when a callback finishes, unblocking cancelTimer().
    std::thread timerThread;             ///< Dedicated thread that sleeps until the next deadline.
    ThreadPool& threadPool;              ///< Pool to which expired callbacks are dispatched.
};

} // namespace core

#endif // TIME_MANAGER_H

