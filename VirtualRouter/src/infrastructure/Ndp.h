// Ndp.h

#ifndef NDP_H
#define NDP_H

#include <PacketStructure.h>
#include <Functions.h>
#include <Encapsulation.h>
#include <RoutingTable.h>
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
    ByteString macAddress; ///< MAC address associated with the IP.
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
        std::atomic<uint16_t> raPreferedLifetime = 900;
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
    void addNdpEntry(const ByteString& targetIp, const ByteString& targetMac, bool proxy = false, bool isStatic = false);

    void resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend);

    void sendNeighborSolicitation(const ByteString& targetIp);

    void sendNeighborAdvertisement(const ByteString& currentMac, ByteString const* targetIp);

    void sendRouteSolicitation(const ByteString& targetIp);

    void sendRouteAdvertisement(const ByteString& targetMac, const ByteString& targetIp);

    void sendRedirectMessage(const ByteString& targetIp, const ByteString& destinationIp);
    
    void sendRedirectIfNeeded(const PacketInfo& originalPacket);

    void receiveNeighborAdvertisement(const IcmpV6Header& receivedNA, const ByteString& sourceIp);
    
    void receiveNeighborSolicitation(const IcmpV6Header& nsHeader, const ByteString& srcIp, const ByteString& srcMac);
    
    void receiveRouteAdvertisement(const IcmpV6Header& receivedRA, const ByteString& sourceIp, const ByteString& srcMac);

    void receiveRedirectMessage(const IcmpV6Header& redirect, const ByteString& sourceIp);

    void shutdown();

    void duplicateAddressDetection(InterfaceConfigs::IPv6State::IPv6Address* address, bool isLinkLocal = false);

    void preformDad(InterfaceConfigs::IPv6State::IPv6Address* addr, bool isLinkLocal);

    void initiateSlaac();

    void addSlaacExclusionPrefix(const ByteString& prefix, bool remove = false);

    void addRaGuardAllowedMac(const ByteString& mac, bool remove = false);

    ByteString* getMac(const ByteString& ip);

private:
    Interface* currentInterface; ///< Pointer to the associated network interface.

    std::deque<ByteString> insertionOrder;
    std::unordered_map<ByteString, NdpCacheEntry> ndpCache; ///< NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<ByteString, NdpCacheEntry> staticNdpCache; ///< Static NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<ByteString, ByteString> proxyEntries; ///< NDP proxy cache mapping IPs to MAC addresses and expiration times.
    std::unordered_set<ByteString> pendingRequests; ///< Tracks ongoing NDP requests.
    std::unordered_map<ByteString, bool> neighborReplyStatus; ///< Tracks NDP neighbor reply statuses.
    std::unordered_map<ByteString, bool> routeReplyStatus; ///< Tracks NDP route reply statuses.
    std::unordered_map<ByteString, std::queue<PacketInfo>> packetQueuePerIp; ///< Packets waiting for NDP resolution.
    std::unordered_set<ByteString> raGuardAllowedMacs; ///< Macs allowed to send RAs.
    std::unordered_map<ByteString, std::chrono::steady_clock::time_point> lastUnsolicitedNaTime; ///< Timestamps for unsolicited NAs.
    std::unordered_map<ByteString, std::chrono::steady_clock::time_point> raReceivedTimestamps; ///< Timestamps for received RAs.
    std::unordered_map<ByteString, uint32_t> nudDelayTimers; ///< NUD delay timers.
    std::unordered_map<ByteString, uint32_t> pendingDadReschedules;
    std::atomic<std::chrono::steady_clock::time_point> lastLogWindowStart;
    std::chrono::steady_clock::time_point lastRaReceiveTime;
    std::atomic<uint32_t> currentNudProbes = 0;
    std::atomic<uint32_t> currentResolvingNeighbors = 0;
    std::atomic<uint32_t> nfsResolutionCount = 0;
    std::unordered_set<ByteString> queuedNudProbes;
    std::unordered_set<ByteString> queuedResolution;

    std::vector<ByteString> slaacExclusionPrefixes;

    mutable std::shared_mutex ndpCacheMutex; ///< Mutex for thread-safe access to the ARP cache.
    std::mutex requestMutex; ///< Mutex for thread-safe access to `pendingRequests`.
    std::mutex neighborReplyStatusMutex; ///< Mutex for thread-safe access to `replyStatus`.
    std::mutex packetQueueMutex; ///< Mutex for thread-safe access to `packetQueuePerIp`.

    std::unordered_set<uint32_t> raTimerIds;
    std::unordered_map<ByteString, uint32_t> nsRetryTimers; ///< Per-IP NS retry timers.
    std::unordered_map<ByteString, uint8_t> nsRetryCount;
    std::unordered_map<ByteString, uint32_t> dadTimers; ///< Per-IP DAD timers.

    std::atomic<bool> running; ///< Indicates whether the NDP service is active.

protected:

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(const ByteString& targetIp, const ByteString& macAddress);

    void onReachableTimeout(const ByteString& targetIp);

    void scheduleNextRA();
    
    void scheduleNeighborSolicitation(const ByteString& targetIp);

    void startNud(const ByteString& targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock);

    void refreshNeighborEntry(const ByteString& targetIp);

    bool shouldLog();

    void retryNud(const ByteString& targetIp);

    void scheduleNeighborEntry(const ByteString& ip);

    ByteString generateMulticastSolicitationAddress(const ByteString& targetIp);

    /**
     * @brief Creates an Neighbor Solicitation packet.
     * @param currentMac The sender's MAC address.
     * @param targetIp The target IP address.
     * @return The constructed Neighbor Solicitation packet.
     */
    PacketInfo neighborSolicitation(const ByteString& targetIp, ByteString const* currentMac);

    /**
     * @brief Creates an Neighbor Advertisment packet.
     * @param currentMac The sender's MAC address.
     * @param targetIp The recipient's IP address.
     * @return The constructed Neighor Advertisement packet.
     */
    PacketInfo neighborAdvertisement(const ByteString& currentMac, ByteString const* targetIp);
     
    /**
     * @brief Creates an Route Solicitation packet.
     * @param currentMac The sender's MAC address.
     * @return The constructed Route Solicitation packet.
     */
    PacketInfo routeSolicitation(const ByteString& currentMac);

    /**
     * @brief Creates an NDP advertisment packet.
     * @param currentMac The sender's MAC address.
     *
     * @return The constructed Route Advertisment packet.
     */
    PacketInfo routeAdvertisement(const ByteString& currentMac);

    Global& global;
};

} // namespace Protocol

#endif // NDP_H
