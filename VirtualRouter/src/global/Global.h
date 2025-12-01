// Global.h

#ifndef GLOBAL_H
#define GLOBAL_H

#include <string>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <ThreadPool.hpp>
#include <TimeManager.h>
#include <TxQueueManager.h>
#include <RxQueueManager.h>
#include <CliEngine.h>
#include <AddressFamily.hpp>
#include <IPAddress.hpp>
#include <map>

#define DEFAULT_HOSTNAME "router"

class Interface;
class VirtualRouter;
enum class AddressFamily : uint8_t;
class CliEngine;
struct HwIfaceInfo;
namespace Protocol
{
    class DhcpServer;
    class Dhcpv6Server;
}

enum class InterfaceType : uint8_t;

/**
 * @struct GlobalConfigs
 * @brief Holds ALL global configurations for the router
 *
 * This is a global configuration manager that holds all global configs for every vrf.
 * This provides atomics for a thread safe way of accessing configs.
 */
struct GlobalConfigs
{
    std::atomic<bool> nsfActive = false;
    std::chrono::steady_clock::time_point nsfStartTime;

    struct Arp
    {
        std::atomic<bool> acceptGratiutous = true;
        std::atomic<bool> incompleteEnabled = true;
        std::atomic<bool> disableProxy = false;
        std::atomic<bool> redirects = false; //TODO
        std::atomic<bool> stickyArp = false;

        std::atomic<uint32_t> incompleteResolveLimit = 1024;
        std::atomic<uint32_t> incompleteRetries = 3;
        std::atomic<uint32_t> incompleteInterval = 5;
        std::atomic<uint32_t> queueSize = 512;

        std::string arpDumpFileLocation; //TODO
        std::atomic<uint32_t> stackTraceSize; //TODO
        std::atomic<uint8_t> stackTraceDepth; //TODO

        struct Neighbor
        {
            uint64_t mac;
            uint32_t interface;
            bool proxy = false;
        };
        std::map<std::string, std::map<uint32_t, Neighbor>> neighbors;
        std::shared_mutex neighborMutex;
    } arp;

    struct Ndp
    {
        std::atomic<bool> refresh = false;
        std::atomic<bool> ndAsRouteOwner = false;
        std::atomic<bool> strictMode = false;

        std::atomic<uint16_t> nudRefreshPeriod = 0;

        std::atomic<uint16_t> cacheExpire = 600;
        std::atomic<uint16_t> loggingRate = 0;
        std::atomic<uint16_t> dadTime = 1000;
        std::atomic<uint16_t> nsfConvergenceTime = 180;
        std::atomic<uint16_t> nsfDadSupressionTime = 180;
        std::atomic<uint16_t> nsfThrottleResolutions = 1000;
        std::atomic<uint16_t> nudLimit = 2048;
        std::atomic<uint16_t> resolutionLimit = 512;

        std::atomic<uint32_t> interfaceLimit = 0;
        std::atomic<uint32_t> reachableTime = 30000;

        // Static Neighbors
        struct Neighbor
        {
            uint32_t interface;
            uint64_t macAddress;
        };
        std::map<IPAddress, Neighbor> neighbors;
        std::shared_mutex neighborMutex;
    } ndp;
};

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
     * @brief Constructs the Global class.
     *
     * Initializes the hostname to the default value and sets IPv6 as disabled.
     * The constructor is private to enforce the Singleton pattern.
     */
    Global(const StartupFiles& stfs = {}, bool enableRouting = false, bool test = false);
    Global(IFileSystem* fs, const StartupFiles& stfs = {}, bool test = false);

    /**
     * @brief Destructs the Global class.
     *
     * The destructor deletes all virtual instances.
     */
    ~Global();

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

    // IPv6 Unicast routing
    void setIPv6UnicastRouting(bool enable) { ipv6RoutingUnicast.store(enable, std::memory_order_relaxed); }
    bool isIPv6UnicastRouting() { return ipv6RoutingUnicast.load(std::memory_order_relaxed); }

    // AAA
    void setAAA(bool enable) { aaaEnabled.store(enable, std::memory_order_relaxed); }
    bool isAAA() {return aaaEnabled.load(std::memory_order_relaxed); }

    // Interfaces
    Interface* addInterface(InterfaceType interfaceType, const HwIfaceInfo& hwInfo, float interfaceId, bool debug);
    Interface* getInterface(uint32_t key);
    std::map<uint32_t, Interface*>& getInterfaceList();
    bool removeInterface(uint32_t key);

    // Routing Instances
    VirtualRouter* addRoutingInstance(const std::string& name);
    VirtualRouter* getRoutingInstance(const std::string& name, AddressFamily = AddressFamily::NONE);
    bool removeRoutingInstance(const std::string& name);
    
    // DHCP
    Protocol::DhcpServer* dhcpServer = nullptr;
    Protocol::Dhcpv6Server* dhcpv6Server = nullptr;

    /**
     * @brief Resets the global state of the router.
     */
    void reset();

private:
    // Delete copy constructor
    Global& operator=(const Global&) = delete;

    std::string hostname = DEFAULT_HOSTNAME;    ///< Hostname of the router.
    std::shared_mutex hostnameMutex;            ///< Mutex protecting the hostname.

    // Boolean options
    std::atomic<bool> ipv6RoutingUnicast = false;   ///< Flag indicating IPv6 enabled.
    std::atomic<bool> aaaEnabled = false;

    // Interfaces
    std::mutex interfaceMutex; ///< Interface list mutex.
    std::map<uint32_t, Interface*> interfaceList; ///< Interface list.

    // Routing Instances
    std::mutex routingInstanceMutex; ///< Routing Instance mutex.
    std::unordered_map<std::string, VirtualRouter*> routingInstances; ///< List of Virtual Routers.
    
public:

    bool routingEnabled = false;
    bool testingMode = false;

    GlobalConfigs configs;
    ThreadPool threadPool;  ///< Global thread pool for off-loading.
    TimeManager timeManager; ///< Global time manager for time keeping.
    CliEngine engine; ///< Global CLI engine for user interface.

    TxQueueManager txMgr; ///< TX Queue Manager
    RxQueueManager rxMgr; ///< RX Queue Manager
};

#endif // GLOBAL_H
