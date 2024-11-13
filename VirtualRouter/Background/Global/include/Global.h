#pragma once

#include <string>
#include <mutex>
#include <memory>

#define DEFAULT_HOSTNAME "router"

class Global {
public:
    static Global& getInstance()
    {
        static Global instance;
        return instance;
    }
    
    std::string& Hostname() { return protectedValue<std::string>(hostname, hostnameMutex); }
    bool& IPv6Enabled() { return protectedValue<bool>(ipv6Enabled, ipv6EnabledMutex); }

    Global(const Global&) = delete;
    Global& operator=(const Global&) = delete;
private:
    Global();
    ~Global() = default;

    template <typename T>
    T& protectedValue(T& value, std::mutex& mxt)
    {
        mxt.lock(); // Manually lock for returning reference

        // Costom lock structure
        struct AutoUnlock {
            std::mutex& mtx;
            ~AutoUnlock() { mtx.unlock(); }
        } AutoUnlock{ mxt };

        return value; // Returns reference to the original value
    }

    // Hostname
    std::string hostname;
    std::mutex hostnameMutex;

    // IPv6 Unicast-Routing
    bool ipv6Enabled;
    std::mutex ipv6EnabledMutex;
};