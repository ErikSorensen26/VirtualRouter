// ThreadPool.h

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    // Enqueue a task and return a future
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // Shutdown the thread pool
    void shutdown();

private:
    // Worker threads
    std::vector<std::thread> workers;

    // Task queue
    std::queue<std::function<void()>> tasks;

    // Synchronization
    std::mutex queueMutex;
    std::condition_variable condition;
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

#endif // THREADPOOL_H
