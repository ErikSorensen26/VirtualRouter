#pragma once

#include <string>
#include <mutex>

/**
 * @def DEFAULT_HOSTNAME
 * @brief Defines the default hostname for the router.
 */
#define DEFAULT_HOSTNAME "router"

/**
 * @class Global
 * @brief Singleton class that manages global configuration settings.
 *
 * The Global class provides a thread-safe mechanism to access and modify
 * global configuration parameters such as the hostname and IPv6 enablement status.
 * It ensures that only one instance of the class exists throughout the application,
 * following the Singleton design pattern.
 */
class Global {
public:
    /**
     * @brief Retrieves the singleton instance of the Global class.
     *
     * This method ensures that only one instance of the Global class exists.
     * Subsequent calls to this method will return a reference to the same instance.
     *
     * @return Global& Reference to the singleton Global instance.
     */
    static Global& getInstance()
    {
        static Global instance;
        return instance;
    }

    /**
     * @brief Retrieves a thread-safe reference to the hostname.
     *
     * This method provides access to the hostname in a thread-safe manner by locking
     * the associated mutex before returning a reference. However, returning a reference
     * after unlocking the mutex can lead to potential race conditions. Consider returning
     * a copy instead to ensure thread safety.
     *
     * @return std::string& Reference to the hostname string.
     */
    std::string getHostname() { return ProtectedValue<std::string>(hostname, hostnameMutex); }

    /**
     * @brief Retrieves a thread-safe reference to the IPv6 enablement status.
     *
     * This method provides access to the IPv6 enablement flag in a thread-safe manner by locking
     * the associated mutex before returning a reference. However, returning a reference
     * after unlocking the mutex can lead to potential race conditions. Consider returning
     * a copy instead to ensure thread safety.
     *
     * @return bool& Reference to the IPv6 enablement flag.
     */
    bool isIPv6Enabled() { return ProtectedValue<bool>(ipv6Enabled, ipv6EnabledMutex); }

    // Delete copy constructor
    Global(const Global&) = delete;
    Global& operator=(const Global&) = delete;
private:

    /**
     * @brief Constructs the Global class.
     *
     * Initializes the hostname to the default value and sets IPv6 as disabled.
     * The constructor is private to enforce the Singleton pattern.
     */
    Global();

    /**
     * @brief Destructs the Global class.
     *
     * The destructor is defaulted as no special cleanup is required.
     */
    ~Global() = default;

    /**
     * @brief Provides thread-safe access to a value by locking its associated mutex.
     *
     * This template function locks the provided mutex, ensures that it is unlocked
     * automatically when the function scope ends, and returns a reference to the value.
     * 
     * @tparam T The type of the value to protect.
     * @param value Reference to the value to be accessed.
     * @param mtx Reference to the mutex associated with the value.
     * @return T& Reference to the protected value.
     */
    template <typename T>
    T ProtectedValue(T& value, std::mutex& mxt)
    {
        mxt.lock(); // Manually lock for returning reference

        // Costom lock structure
        struct AutoUnlock {
            std::mutex& mtx;
            ~AutoUnlock() { mtx.unlock(); }
        } AutoUnlock{ mxt };

        return value; // Returns reference to the original value
    }

    std::string hostname;           ///< Hostname of the router.
    std::mutex hostnameMutex;       ///< Mutex protecting the hostname.

    bool ipv6Enabled;               ///< Flag indicating IPv6 enabled.
    std::mutex ipv6EnabledMutex;    ///< Mutex protecting the IPv6 enablement flag.
};
