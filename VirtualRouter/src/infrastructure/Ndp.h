// Ndp.h

#ifndef NDP_H
#define NDP_H

#include <queue>

#include "packet/PacketStructure.h"
#include "interface/Interface.h"

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
    uint64_t macAddress; ///< MAC address associated with the IP.
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
    void addNdpEntry(IPv6Address targetIp, uint64_t targetMac, bool proxy = false, bool isStatic = false);

    void resolveAndSend(IPv6Address targetIp, PacketBuilder& packetToSend);

    void sendNeighborSolicitation(IPv6Address targetIp);

    void sendNeighborAdvertisement(uint64_t currentMac, IPv6Address targetIp);

    void sendNeighborAdvertisement();

    void sendRouteSolicitation(IPv6Address targetIp);

    void sendRouteAdvertisement(uint64_t targetMac, IPv6Address targetIp);

    void sendRedirectMessage(IPv6Address targetIp, IPv6Address destinationIp);
    
    void sendRedirectIfNeeded(const PacketInfo& originalPacket, const uint8_t* pkt);

    void receiveNeighborAdvertisement(const Icmpv6Header& receivedNA, IPv6Address sourceIp);
    
    void receiveNeighborSolicitation(const Icmpv6Header& nsHeader, IPv6Address srcIp, uint64_t srcMac);
    
    void receiveRouteAdvertisement(const Icmpv6Header& receivedRA, IPv6Address sourceIp, uint64_t srcMac);

    void receiveRedirectMessage(const Icmpv6Header& redirect, IPv6Address sourceIp);

    void shutdown();

    void duplicateAddressDetection(InterfaceConfigs::IPv6State::IPv6Address& address, bool isLinkLocal = false);

    void preformDad(InterfaceConfigs::IPv6State::IPv6Address& addr, bool isLinkLocal);

    void initiateSlaac();

    void addSlaacExclusionPrefix(IPv6Address prefix, bool remove = false);

    void addRaGuardAllowedMac(uint64_t mac, bool remove = false);

    uint8_t* getMac(uint8_t* out, IPv6Address ip);

private:
    Interface* currentInterface; ///< Pointer to the associated network interface.

    std::vector<IPv6Address> insertionOrder;
    std::unordered_map<IPv6Address, NdpCacheEntry> ndpCache; ///< NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<IPv6Address, NdpCacheEntry> staticNdpCache; ///< Static NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<IPv6Address, uint64_t> proxyEntries; ///< NDP proxy cache mapping IPs to MAC addresses and expiration times.
    std::unordered_set<IPv6Address> pendingRequests; ///< Tracks ongoing NDP requests.
    std::unordered_map<IPv6Address, bool> neighborReplyStatus; ///< Tracks NDP neighbor reply statuses.
    std::unordered_map<IPv6Address, bool> routeReplyStatus; ///< Tracks NDP route reply statuses.
    std::unordered_map<IPv6Address, std::queue<PacketBuilder>> packetQueuePerIp; ///< Packets waiting for NDP resolution.
    std::unordered_set<uint64_t> raGuardAllowedMacs; ///< Macs allowed to send RAs.
    std::unordered_map<IPv6Address, std::chrono::steady_clock::time_point> lastUnsolicitedNaTime; ///< Timestamps for unsolicited NAs.
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> raReceivedTimestamps; ///< Timestamps for received RAs.
    std::unordered_map<IPv6Address, uint32_t> nudDelayTimers; ///< NUD delay timers.
    std::unordered_map<IPv6Address, uint32_t> pendingDadReschedules;
    std::atomic<std::chrono::steady_clock::time_point> lastLogWindowStart;
    std::chrono::steady_clock::time_point lastRaReceiveTime;
    std::atomic<uint32_t> currentNudProbes = 0;
    std::atomic<uint32_t> currentResolvingNeighbors = 0;
    std::atomic<uint32_t> nfsResolutionCount = 0;
    std::unordered_set<IPv6Address> queuedNudProbes;
    std::unordered_set<IPv6Address> queuedResolution;

    std::vector<IPv6Address> slaacExclusionPrefixes;

    mutable std::shared_mutex ndpCacheMutex; ///< Mutex for thread-safe access to the ARP cache.
    std::mutex requestMutex; ///< Mutex for thread-safe access to `pendingRequests`.
    std::mutex neighborReplyStatusMutex; ///< Mutex for thread-safe access to `replyStatus`.
    std::mutex packetQueueMutex; ///< Mutex for thread-safe access to `packetQueuePerIp`.

    std::unordered_set<uint32_t> raTimerIds;
    std::unordered_map<IPv6Address, uint32_t> nsRetryTimers; ///< Per-IP NS retry timers.
    std::unordered_map<IPv6Address, uint8_t> nsRetryCount;
    std::unordered_map<IPv6Address, uint32_t> dadTimers; ///< Per-IP DAD timers.

    std::atomic<bool> running; ///< Indicates whether the NDP service is active.

protected:

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(IPv6Address targetIp, uint64_t macAddress);

    void onReachableTimeout(IPv6Address targetIp);

    void scheduleNextRA();
    
    void scheduleNeighborSolicitation(IPv6Address targetIp);

    void startNud(IPv6Address targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock);

    void refreshNeighborEntry(IPv6Address targetIp);

    bool shouldLog();

    void retryNud(IPv6Address targetIp);

    void scheduleNeighborEntry(IPv6Address ip);

    IPv6Address generateMulticastSolicitationAddress(IPv6Address targetIp);

    /**
     * @brief Creates an Neighbor Solicitation packet.
     * 
     * @param packet Packet being built.
     * @param currentMac The sender's MAC address.
     * @param targetIp The target IP address.
     */
    void neighborSolicitation(PacketBuilder& packet, IPv6Address targetIp, uint64_t* currentMac);

    /**
     * @brief Creates an Neighbor Advertisment packet.
     * @param currentMac The sender's MAC address.
     * @param targetIp The recipient's IP address.
     *
     * @return The constructed Neighor Advertisement packet.
     */
    void neighborAdvertisement(PacketBuilder& packet, uint64_t currentMac, IPv6Address* targetIp);
     
    /**
     * @brief Creates an Route Solicitation packet.
     * @param currentMac The sender's MAC address.
     * @return The constructed Route Solicitation packet.
     */
    void routeSolicitation(PacketBuilder& packet, uint64_t currentMac);

    /**
     * @brief Creates an NDP advertisment packet.
     * @param currentMac The sender's MAC address.
     *
     * @return The constructed Route Advertisment packet.
     */
    void routeAdvertisement(PacketBuilder& packet, uint64_t currentMac);

    Global& global;
};

} // namespace Protocol

#endif // NDP_H
