// ThreadPool.h

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

/**
 * @file ThreadPool.h
 * @brief Defines the ThreadPool class for managing a pool of worker threads to execute tasks asynchronously.
 */

/**
 * @class ThreadPool
 * @brief Manages a pool of worker threads to execute tasks asynchronously.
 *
 * The `ThreadPool` class provides a convenient way to manage multiple threads that can execute
 * tasks concurrently. It maintains a fixed number of worker threads that continuously fetch
 * and execute tasks from a task queue. Tasks can be enqueued with any callable object, and
 * the thread pool ensures their execution in a thread-safe manner.
 *
 * @note The number of threads should be chosen based on the application's concurrency requirements
 *       and the system's hardware capabilities.
 */
class ThreadPool 
{
public:

    /**
     * @brief Constructs a ThreadPool with a specified number of worker threads.
     *
     * This constructor initializes the thread pool by launching the specified number of worker threads.
     * Each worker thread continuously retrieves and executes tasks from the task queue until the pool is
     * stopped.
     *
     * @param numThreads The number of worker threads to create in the thread pool.
     *
     * @throws std::invalid_argument If `numThreads` is zero.
     */
    ThreadPool(size_t numThreads);

    /**
     * @brief Destructor that stops the thread pool and joins all worker threads.
     *
     * Ensures that all worker threads are properly terminated and joined before the ThreadPool object is destroyed.
     */
    ~ThreadPool();

    /**
     * @brief Enqueues a task for execution and returns a future to retrieve the result.
     *
     * This method allows clients to submit tasks to the thread pool. It accepts any callable object
     * with arguments, wraps it into a packaged task, and enqueues it for execution by the worker threads.
     * The method returns a `std::future` that can be used to obtain the result of the task.
     *
     * @tparam F The type of the callable object.
     * @tparam Args The types of the arguments to pass to the callable object.
     * @param f The callable object to execute.
     * @param args The arguments to pass to the callable object.
     *
     * @return A `std::future` representing the result of the task.
     *
     * @throws std::runtime_error If the thread pool has been stopped and cannot accept new tasks.
     *
     * @note The use of `std::result_of` is deprecated in C++17 and removed in C++20.
     *       It is recommended to use `std::invoke_result` instead.
     */
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    /**
     * @brief Stops the thread pool and joins all worker threads.
     *
     * This method gracefully shuts down the thread pool by signaling all worker threads to stop processing tasks.
     * It then joins each worker thread to ensure proper termination. After calling this method, the thread pool
     * cannot accept new tasks.
     */
    void shutdown();

private:

    /**
     * @brief Worker threads that execute tasks from the task queue.
     *
     * Each worker thread runs a loop that continuously retrieves and executes tasks from the task queue.
     * The loop exits when the thread pool is stopped and there are no remaining tasks.
     */
    std::vector<std::thread> workers;

    /**
     * @brief Queue that holds tasks to be executed by the worker threads.
     *
     * The task queue stores tasks as `std::function<void()>`, allowing any callable object to be enqueued.
     */
    std::queue<std::function<void()>> tasks;

    /**
     * @brief Mutex for synchronizing access to the task queue.
     *
     * Ensures that multiple threads can safely enqueue and dequeue tasks without causing data races.
     */
    std::mutex queueMutex;

    /**
     * @brief Condition variable to notify worker threads of new tasks or shutdown signals.
     *
     * Worker threads wait on this condition variable when the task queue is empty. They are notified
     * when new tasks are enqueued or when the thread pool is stopped.
     */
    std::condition_variable condition;

    /**
     * @brief Atomic flag indicating whether the thread pool is stopping.
     *
     * When set to `true`, worker threads will stop processing tasks and exit their execution loops.
     */
    std::atomic<bool> stop;
};

// Constructor: Launch worker threads
inline ThreadPool::ThreadPool(size_t numThreads) : stop(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back(
            [this] {
                while (true) {
                    std::function<void()> task;

                    {   // Acquire lock
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, 
                            [this]{ return this->stop.load() || !this->tasks.empty(); });
                        if (this->stop.load() && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    // Execute the task
                    task();
                }
            }
        );
    }
}

// Destructor: Join all threads
inline ThreadPool::~ThreadPool() {
    shutdown();
}

// Enqueue method
template <class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::lock_guard<std::mutex> lock(queueMutex);

        if (stop.load())
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

// Shutdown method
inline void ThreadPool::shutdown() {
    stop.store(true);
    condition.notify_all();
    for (std::thread &worker: workers)
        if (worker.joinable())
            worker.join();
}

#endif // THREADPOOL_HPP
