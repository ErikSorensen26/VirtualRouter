// Ndp.h

#ifndef NDP_H
#define NDP_H

#include <PacketStructure.h>
#include <Functions.h>
#include <Encapsulation.h>
#include <queue>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include <TimeManager.h>
#include <Interface.h>

class Internal_NdpTest;

/**
 * @namespace Protocol
 * Contains network protocol implementations.
 */
namespace Protocol 
{

/**
 * @enum NeighborStates
 * Represents neighbor states for NUD.
 */
enum class NudState
{
    ACTIVE,
    REACHABLE,
    STALE,
    DELAY,
    PROBE,
    UNREACHABLE
};


/**
 * @struct NdpCacheEntry
 * Represents a single ARP cache entry, including the MAC address and expiration time.
 */
struct NdpCacheEntry
{
    uint8_t macAddress[6]; ///< MAC address associated with the IP.
    std::chrono::steady_clock::time_point expiryTime; ///< Expiration time for this cache entry.
    NudState state = NudState::ACTIVE;
    uint32_t timerId = 0;
    uint8_t nudGroup = 1;
    uint32_t nudRetryTimerId = 0;
};

/**
 * @class Ndp
 * Handles ARP functionality, including cache management, ARP request/reply handling, and packet resolution.
 */
class Ndp
{
public:
    friend class ::Internal_NdpTest;

    /**
     * @struct Configs
     * @brief Holds configurations for NDP
     */
    struct Configs
    {
        /**
         * @enum Preference
         * @brief represents the routers preference
         */
        enum class Preference { HIGH, MEDIUM, LOW };

        /**
         * @enum RaGuardMode
         * @brief represents all RA-Guard modes.
         */
        enum class RaGuardMode : uint8_t { BLOCK_ALL, TRUSTED, MAC_WHITELIST};

        std::atomic<bool> slaacEnabled = false;
        std::atomic<bool> advertisementInterval = false;
        std::atomic<bool> autoConfigDefaultRoute = false;
        std::atomic<bool> autoConfigPrefix = false;
        std::atomic<bool> destinationGuard = false;
        std::atomic<bool> managedConfigFlag = false; 
        std::atomic<bool> otherConfigFlag = false;
        std::atomic<bool> naGlean = false;
        std::atomic<bool> nudIgp = false;
        std::atomic<bool> mtuSuppress = false;
        std::atomic<bool> suppressRA = false;
        std::atomic<bool> suppressNA = false;
        std::atomic<bool> raSuppressAll = false;
        std::atomic<bool> raHopLimitUnspecified = false;
        std::atomic<bool> redirects = false;

        std::atomic<uint16_t> dadAttempts = 1;
        std::atomic<uint16_t> raLifetime = 1800;
        std::atomic<uint16_t> raPreferredLifetime = 900;
        std::atomic<uint16_t> routerLifetime = 1800;

        std::atomic<uint32_t> nsInterval = 1000;
        std::atomic<uint32_t> raRateLimit = 5;

        // Global
        std::atomic<bool> refresh;
        std::atomic<uint16_t> loggingRate;
        std::atomic<uint16_t> cacheExpire;
        std::atomic<uint16_t> dadTime;
        std::atomic<uint32_t> reachableTime;
        std::atomic<uint32_t> interfaceLimit;

        bool refreshLocal = false;
        bool cacheExpireLocal = false;
        bool dadTimeLocal = false;
        bool interfaceLimitLocal = false;
        bool reachableTimeLocal = false;
        bool loggingRateLocal = false;

        std::atomic<Preference> preference = Preference::MEDIUM;
        std::atomic<RaGuardMode> raGuardMode = RaGuardMode::BLOCK_ALL;

        std::shared_mutex configMutex;

        uint8_t nudBase = 3;
        uint16_t nudBaseInterval = 1000;
        uint16_t nudBaseAttempts = 3;
        uint16_t nudFinalWait = 60000;

        bool raIntervalMS = true;
        uint32_t raInterval = 600000;
        uint32_t raIntervalMin = 3000;

    } configs;

    /**
     * @brief Constructor for the NDP class.
     * @param CurrentInterface Reference to the network interface associated with this NDP instance.
     */
    explicit Ndp(Interface& CurrentInterface);

    void initializeNdp();

    /**
     * @brief Destructor for the Ndp class.
     * Ensures a clean shutdown of threads and resources.
     */
    ~Ndp();

    /**
     * @brief Adds an ndp entry to the arp cache table
     *
     * @parap targetIp The target IP of the resolved arp entry.
     * @param mac The MAC of the resolved arp entry.
     */
    void addNdpEntry(const IPAddress& targetIp, uint64_t targetMac, bool proxy = false, bool isStatic = false);

    void resolveAndSend(const uint8_t* targetIp, PacketBuilder& packetToSend);

    void sendNeighborSolicitation(const IPAddress& targetIp);

    void sendNeighborAdvertisement(const uint8_t* currentMac, const uint8_t* targetIp);

    void sendRouteSolicitation(const uint8_t* targetIp);

    void sendRouteAdvertisement(const uint8_t* targetMac, const uint8_t* targetIp);

    void sendRedirectMessage(const uint8_t* targetIp, const uint8_t* destinationIp);
    
    void sendRedirectIfNeeded(const PacketInfo& originalPacket, const uint8_t* pkt);

    void receiveNeighborAdvertisement(const Icmpv6Header& receivedNA, const uint8_t* sourceIp);
    
    void receiveNeighborSolicitation(const Icmpv6Header& nsHeader, const uint8_t* srcIp, const uint8_t* srcMac);
    
    void receiveRouteAdvertisement(const Icmpv6Header& receivedRA, const uint8_t* sourceIp, const uint8_t* srcMac);

    void receiveRedirectMessage(const Icmpv6Header& redirect, const uint8_t* sourceIp);

    void shutdown();

    void duplicateAddressDetection(InterfaceConfigs::IPv6State::IPv6Address* address, bool isLinkLocal = false);

    void preformDad(InterfaceConfigs::IPv6State::IPv6Address* addr, bool isLinkLocal);

    void initiateSlaac();

    void addSlaacExclusionPrefix(const IPAddress& prefix, bool remove = false);

    void addRaGuardAllowedMac(const uint8_t* mac, bool remove = false);

    uint8_t* getMac(uint8_t* out, const uint8_t* ip);

private:
    Interface* currentInterface; ///< Pointer to the associated network interface.

    std::vector<IPAddress> insertionOrder;
    std::unordered_map<IPAddress, NdpCacheEntry> ndpCache; ///< NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<IPAddress, NdpCacheEntry> staticNdpCache; ///< Static NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<IPAddress, uint64_t> proxyEntries; ///< NDP proxy cache mapping IPs to MAC addresses and expiration times.
    std::unordered_set<IPAddress> pendingRequests; ///< Tracks ongoing NDP requests.
    std::unordered_map<IPAddress, bool> neighborReplyStatus; ///< Tracks NDP neighbor reply statuses.
    std::unordered_map<IPAddress, bool> routeReplyStatus; ///< Tracks NDP route reply statuses.
    std::unordered_map<IPAddress, std::queue<PacketBuilder>> packetQueuePerIp; ///< Packets waiting for NDP resolution.
    std::unordered_set<uint64_t> raGuardAllowedMacs; ///< Macs allowed to send RAs.
    std::unordered_map<IPAddress, std::chrono::steady_clock::time_point> lastUnsolicitedNaTime; ///< Timestamps for unsolicited NAs.
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> raReceivedTimestamps; ///< Timestamps for received RAs.
    std::unordered_map<IPAddress, uint32_t> nudDelayTimers; ///< NUD delay timers.
    std::unordered_map<IPAddress, uint32_t> pendingDadReschedules;
    std::atomic<std::chrono::steady_clock::time_point> lastLogWindowStart;
    std::chrono::steady_clock::time_point lastRaReceiveTime;
    std::atomic<uint32_t> currentNudProbes = 0;
    std::atomic<uint32_t> currentResolvingNeighbors = 0;
    std::atomic<uint32_t> nfsResolutionCount = 0;
    std::unordered_set<IPAddress> queuedNudProbes;
    std::unordered_set<IPAddress> queuedResolution;

    std::vector<IPAddress> slaacExclusionPrefixes;

    mutable std::shared_mutex ndpCacheMutex; ///< Mutex for thread-safe access to the ARP cache.
    std::mutex requestMutex; ///< Mutex for thread-safe access to `pendingRequests`.
    std::mutex neighborReplyStatusMutex; ///< Mutex for thread-safe access to `replyStatus`.
    std::mutex packetQueueMutex; ///< Mutex for thread-safe access to `packetQueuePerIp`.

    std::unordered_set<uint32_t> raTimerIds;
    std::unordered_map<IPAddress, uint32_t> nsRetryTimers; ///< Per-IP NS retry timers.
    std::unordered_map<IPAddress, uint8_t> nsRetryCount;
    std::unordered_map<IPAddress, uint32_t> dadTimers; ///< Per-IP DAD timers.

    std::atomic<bool> running; ///< Indicates whether the NDP service is active.

protected:

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(const IPAddress& targetIp, const uint8_t* macAddress);

    void onReachableTimeout(const IPAddress& targetIp);

    void scheduleNextRA();
    
    void scheduleNeighborSolicitation(const IPAddress& targetIp);

    void startNud(const IPAddress& targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock);

    void refreshNeighborEntry(const IPAddress& targetIp);

    bool shouldLog();

    void retryNud(const IPAddress& targetIp);

    void scheduleNeighborEntry(const IPAddress ip);

    uint8_t* generateMulticastSolicitationAddress(uint8_t* out, const uint8_t* targetIp);

    /**
     * @brief Creates an Neighbor Solicitation packet.
     * 
     * @param packet Packet being built.
     * @param currentMac The sender's MAC address.
     * @param targetIp The target IP address.
     */
    void neighborSolicitation(PacketBuilder& packet, const IPAddress& targetIp, const uint8_t* currentMac);

    /**
     * @brief Creates an Neighbor Advertisment packet.
     * @param currentMac The sender's MAC address.
     * @param targetIp The recipient's IP address.
     *
     * @return The constructed Neighor Advertisement packet.
     */
    void neighborAdvertisement(PacketBuilder& packet, const uint8_t* currentMac, const uint8_t* targetIp);
     
    /**
     * @brief Creates an Route Solicitation packet.
     * @param currentMac The sender's MAC address.
     * @return The constructed Route Solicitation packet.
     */
    void routeSolicitation(PacketBuilder& packet, const uint8_t* currentMac);

    /**
     * @brief Creates an NDP advertisment packet.
     * @param currentMac The sender's MAC address.
     *
     * @return The constructed Route Advertisment packet.
     */
    void routeAdvertisement(PacketBuilder& packet, const uint8_t* currentMac);

    Global& global;
};

} // namespace Protocol

#endif // NDP_H
