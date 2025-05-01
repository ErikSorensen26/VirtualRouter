// Global.h

#ifndef GLOBAL_H
#define GLOBAL_H

#include <string>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <VirtualRouter.h>
#include <InterfacePairHash.hpp>

class Interface;
class VirtualRouter;
enum class AddressFamily;
namespace Protocol
{
    class DhcpServer;
    class Dhcpv6Server;
}

enum class InterfaceType;

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
class Global 
{
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

    // Set hostname (protected by hostnameMutex)
    void setHostname(const std::string& name)
    {
        std::unique_lock<std::shared_mutex> lock(hostnameMutex);
        hostname = name;
    }

    std::string getHostname()
    {
        std::shared_lock<std::shared_mutex> lock(hostnameMutex);
        return hostname;
    }

    void resetDefault()
    {
        std::lock_guard<std::mutex> lock(routingInstanceMutex);
        delete routingInstances["default"];
        routingInstances.erase("default");
        routingInstances["default"] = new VirtualRouter("default");
    }

    // IPv6 Unicast routing
    void setIPv6UnicastRouting(bool enable) { ipv6RoutingUnicast.store(enable, std::memory_order_relaxed); }
    bool isIPv6UnicastRouting() { return ipv6RoutingUnicast.load(std::memory_order_relaxed); }

    // AAA
    void setAAA(bool enable) { aaaEnabled.store(enable, std::memory_order_relaxed); }
    bool isAAA() {return aaaEnabled.load(std::memory_order_relaxed); }

    // Interfaces
    Interface* addInterface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, bool debug);
    Interface* getInterface(InterfaceType type, float interfaceID);
    bool removeInterface(InterfaceType type, float interfaceId);

    // Routing Instances
    VirtualRouter* addRoutingInstance(const std::string& name);
    VirtualRouter* getRoutingInstance(const std::string& name, AddressFamily = AddressFamily::NONE);
    bool removeRoutingInstance(const std::string& name);
    
    // DHCP
    Protocol::DhcpServer* dhcpServer = nullptr;
    Protocol::Dhcpv6Server* dhcpv6Server = nullptr;

private:
    // Delete copy constructor
    Global(const Global&) = delete;
    Global& operator=(const Global&) = delete;

    /**
     * @brief Constructs the Global class.
     *
     * Initializes the hostname to the default value and sets IPv6 as disabled.
     * The constructor is private to enforce the Singleton pattern.
     */
    Global() {};

    /**
     * @brief Destructs the Global class.
     *
     * The destructor is defaulted as no special cleanup is required.
     */
    ~Global() = default;

    std::string hostname = DEFAULT_HOSTNAME;    ///< Hostname of the router.
    std::shared_mutex hostnameMutex;            ///< Mutex protecting the hostname.

    // Boolean options
    std::atomic<bool> ipv6RoutingUnicast = false;   ///< Flag indicating IPv6 enabled.
    std::atomic<bool> aaaEnabled = false;

    // Interfaces
    std::mutex interfaceMutex; ///< Interface list mutex.
    std::map<std::pair<InterfaceType, float>, Interface*> interfaceList; ///< Interface list.

    // Routing Instances
    std::mutex routingInstanceMutex; ///< Routing Instance mutex.
    std::unordered_map<std::string, VirtualRouter*> routingInstances; ///< List of Virtual Routers.
    
public:

    /**
     * @brief Clears all stored configurations and resets the class
     *
     * This method ensures that everything is safely reset.
     *
     * @note This is mainly for testing purposes, this is dangourus to use on a active router.
     */
    void resetInstance()
    {
        setHostname("router");

        setIPv6UnicastRouting(false);
        setAAA(false);
    }
};

#endif // GLOBAL_H
