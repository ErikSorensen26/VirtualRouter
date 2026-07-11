/**
 * @file TimeManager.h
 * @brief Deadline-based timer manager that dispatches callbacks through a ThreadPool.
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <functional>
#include <map>
#include <unordered_map>
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
 * A dedicated thread sleeps until the next deadline, then submits the callback
 * to the pool. Supports one-shot, recurring, and limited-recurring (N ticks
 * then a final callback) timers. All state is guarded by a single mutex.
 *
 * The @ref ThreadPool must outlive this object. stopTimer() (called by the
 * destructor) joins the worker thread and then waits for every callback
 * already handed to the pool, so no callback can touch a destroyed instance.
 *
 * @warning Do not call stopTimer() from within a timer callback; it waits for
 * all in-flight callbacks (including the caller) and would deadlock.
 *
 * @see ControlScheduler
 */
class TimeManager
{
public:
    /** @brief Starts the internal timer thread; callbacks dispatch to @p pool. */
    TimeManager(ThreadPool& pool);

    /** @brief Stops the timer thread and waits for all in-flight callbacks. */
    ~TimeManager();

    /**
     * @brief Registers a one-shot timer firing at an absolute time point.
     * @return Unique timer ID; pass to cancelTimer() to abort before firing.
     */
    uint32_t addTimer(std::chrono::steady_clock::time_point expirationTime, std::function<void(uint32_t)> callback);

    /**
     * @brief Registers a timer that fires `repeated` every @p interval,
     * `repeatCount` times, then invokes @p finalCallback (if any).
     * @return Unique timer ID.
     */
    uint32_t addLimitedRecurringTimer(std::chrono::milliseconds interval, size_t repeatCount, std::function<void(uint32_t)> repeated, std::function<void(uint32_t)> finalCallback = nullptr);

    /**
     * @brief Registers a recurring timer that fires until cancelled.
     * @return Unique timer ID.
     */
    uint32_t addRecurringTimer(std::chrono::milliseconds interval, std::function<void(uint32_t)> callback);

    /**
     * @brief Cancels a timer by ID.
     *
     * If the callback is currently executing this blocks until it returns
     * (unless called from within the callback itself, which returns
     * immediately; the timer is not rescheduled either way).
     *
     * @return true if found and cancelled; false if already fired or unknown.
     */
    bool cancelTimer(uint32_t timerId);

    /**
     * @brief Reschedules a recurring timer to fire @p newInterval from now; if
     * currently executing, the new interval applies from the next reschedule.
     * Unknown IDs are ignored.
     */
    void updateInterval(uint32_t timerId, std::chrono::milliseconds newInterval);

    /**
     * @brief Signals the timer thread to stop, joins it, and waits for all
     * in-flight callbacks. After it returns, no callback is running or
     * pending in the pool. Called automatically by the destructor.
     */
    void stopTimer();

private:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Callback = std::function<void(uint32_t)>;
    using Queue = std::multimap<TimePoint, uint32_t>;

    /**
     * @brief Complete state for one registered timer; lives in `timers` from
     * registration until the timer dies. All fields guarded by `mutex`.
     */
    struct TimerState
    {
        Callback callback;                     ///< User callback; receives the timer ID.
        std::chrono::milliseconds interval{0}; ///< Repeat interval; 0 for one-shot timers.
        Queue::iterator queuePos;              ///< Position in `queue`; valid iff `queued`.
        bool queued = false;                   ///< True while waiting in `queue`.
        bool executing = false;                ///< True while the callback runs on a pool thread.
        bool cancelled = false;                ///< Set by cancelTimer() while executing; blocks reschedule.
        std::thread::id executingThread{};     ///< For self-cancel detection.
        size_t repeatCount = 0;                ///< Ticks completed (limited-recurring).
        size_t repeatLimit = 0;                ///< Ticks before the final callback; 0 = unlimited.
        Callback finalCallback;                ///< Invoked after the last tick (limited-recurring).
    };

    void Run(); ///< Worker loop: sleeps until the next deadline and dispatches expired timers.
    void runSingleTimer(uint32_t id); ///< Runs one dispatched callback, then reschedules/retires the timer.

    std::atomic<uint32_t> nextTimerId; ///< Monotonic ID counter (IDs never reused).
    std::atomic<bool> stop;
    std::mutex mutex;                    ///< Guards `queue` and `timers`.
    std::condition_variable cv;          ///< Wakes the worker on new timer / stop.
    std::condition_variable timerDoneCV; ///< Notified when a callback finishes (unblocks cancelTimer).

    Queue queue;                                     ///< Pending timers sorted by expiration.
    std::unordered_map<uint32_t, TimerState> timers; ///< All live timers by ID.

    std::atomic<uint32_t> dispatched{0}; ///< Callbacks handed to the pool but not yet finished.
    std::thread timerThread;
    ThreadPool& threadPool;
};

} // namespace core

#endif // TIME_MANAGER_H
