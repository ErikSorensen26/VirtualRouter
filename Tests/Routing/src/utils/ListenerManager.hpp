// ListenerManager

#ifndef LISTENER_MANAGER_HPP
#define LISTENER_MANAGER_HPP

#include <functional>
#include <future>
#include <chrono>
#include <vector>
#include <mutex>

class ListenerManager
{
public:
    using Listener = std::function<bool()>;

    void addListener(Listener listener, int timeoutMs = 1000, std::function<void(bool)> callback = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners.emplace_back(std::async(std::launch::async, [=]() {
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs))
            {
                if (listener())
                {
                    if (callback) callback(true);
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (callback) callback(false);
            return false;
        }));
    }

    bool waitAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool allSucceeded = true;
        for (auto& fut : listeners)
        {
            if (!fut.get())
                allSucceeded = false;
        }
        listeners.clear();
        return allSucceeded;
    }

private:
    std::vector<std::future<bool>> listeners;
    std::mutex mutex_;
};

#endif // LISTENER_MANAGER_HPP
