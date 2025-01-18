// Profiler.hpp

#ifndef PROFILER_HPP
#define PROFILER_HPP

#include <chrono>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <mutex>

class Profiler
{
public:
    // Deleted copy construct and assignment operator to enforce singleton
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // Static method to access the singleton instance
    static Profiler& getInstance()
    {
        static Profiler instance; // Guaranteed to be destroyed and instantiated on first use
        return instance;
    }

    void notify(const std::string& description)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::high_resolution_clock::now();
        // Calculate time since start
        std::chrono::duration<double, std::nano> elapsed = now - start_time_;
        events_.emplace_back(Event{ elapsed.count(), description });
    }

    // Manually print all recorded events
    void print() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "------ Profiling Report ------\n";
        std::cout << std::fixed << std::setprecision(0);
        for (const auto& event : events_)
        {
            std::cout << "[" << event.time_ms << " ms] " << event.description << "\n";
        }
        std::cout << "------------------------------\n";
    }

    // Destructor prints all events
    ~Profiler()
    {
        print();
    }
private:
    // Private constructor for singleton
    Profiler() : start_time_(std::chrono::high_resolution_clock::now()) {}

    struct Event
    {
        double time_ms; // Time in milliseconds since start
        std::string description;
    };

    std::chrono::high_resolution_clock::time_point start_time_;
    std::vector<Event> events_;
    mutable std::mutex mutex_; // To ensure thread safety
};

#endif // PROFILER_HPP
