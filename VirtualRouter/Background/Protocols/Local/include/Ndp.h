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
#include <condition_variable>
#include <atomic>
#include <vector>
#include <thread>
#include <TimeManager.h>

// Forward declarations
class Interface;

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
    REACHABLE,
    STALE,
    DELAY,
    PROBE
};


/**
 * @struct NdpCacheEntry
 * Represents a single ARP cache entry, including the MAC address and expiration time.
 */
struct NdpCacheEntry
{
    ByteString macAddress; ///< MAC address associated with the IP.
    std::chrono::steady_clock::time_point expiryTime; ///< Expiration time for this cache entry.
    NudState state;
    uint32_t timerId;
};

/**
 * @class Ndp
 * Handles ARP functionality, including cache management, ARP request/reply handling, and packet resolution.
 */
class Ndp
{
public:
    /**
     * @brief Constructor for the NDP class.
     * @param CurrentInterface Reference to the network interface associated with this NDP instance.
     */
    explicit Ndp(Interface& CurrentInterface);

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
    void addNdpEntry(const ByteString& targetIp, const ByteString& targetMac);

    void resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend);

    void sendNeighborSolicitation(const ByteString& targetIp);

    void sendNeighborAdvertisement(const ByteString& currentMac, ByteString const* targetIp);

    void sendRouteSolicitation(const ByteString& targetIp);

    void sendRouteAdvertisement(const ByteString& targetMac, const ByteString& targetIp);

    void receiveNeighborAdvertisement(const IcmpV6Header& receivedNA);
    
    void receiveRouteAdvertisement(const IcmpV6Header& receivedRA);

    ByteString* getMac(const ByteString& ip);

    void shutdown();

    void autoConfig();

    void duplicateAddressDetection(bool localLink = false);

    /**
     * @struct Configs
     * @brief Holds configurations for NDP
     */
    struct Configs
    {
        /**
         * @enum Preference
         * @represents the routers preference
         */
        enum class Preference { HIGH, MEDIUM, LOW };

        std::atomic<uint8_t> nudRetries = 3;
        std::atomic<uint16_t> cacheExpire = 600;
        std::atomic<uint16_t> dadAttempts = 1;
        std::atomic<uint16_t> dadTime = 1000;
        std::atomic<uint16_t> lifetime = 1800;
        std::atomic<uint16_t> reachableTime = 30000;
        std::atomic<uint32_t> interfaceLimit = 1024;
        std::atomic<uint32_t> nsInterval = 1000;
        std::atomic<uint32_t> raInterval = 600000;
        std::atomic<bool> advertisementInterval = false;
        std::atomic<bool> autoConfigDefaultRoute = false;
        std::atomic<bool> autoConfigPrefix = false;
        std::atomic<bool> destinationGuard = false;
        std::atomic<bool> managedConfigFlag = false; 
        std::atomic<bool> otherConfigFlag = false;
        std::atomic<bool> framedIPv6Prefix = false;
        std::atomic<bool> naGlean = false;
        std::atomic<bool> nudIgp = false;
        std::atomic<bool> mtuSuppress = false;
        std::atomic<bool> suppressRA = false;
        std::atomic<bool> suppressNA = false;
        std::atomic<Preference> preference = Preference::MEDIUM;

    } configs;

private:
    Interface* currentInterface; ///< Pointer to the associated network interface.

    std::unordered_map<ByteString, NdpCacheEntry> ndpCache; ///< NDP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_set<ByteString> pendingRequests; ///< Tracks ongoing NDP requests.
    std::unordered_map<ByteString, std::shared_ptr<std::atomic<bool>>> neighborReplyStatus; ///< Tracks NDP neighbor reply statuses.
    std::unordered_map<ByteString, std::shared_ptr<std::atomic<bool>>> routeReplyStatus; ///< Tracks NDP route reply statuses.
    std::unordered_map<ByteString, std::queue<PacketInfo>> packetQueuePerIp; ///< Packets waiting for NDP resolution.

    mutable std::shared_mutex ndpCacheMutex; ///< Mutex for thread-safe access to the ARP cache.
    std::mutex requestMutex; ///< Mutex for thread-safe access to `pendingRequests`.
    std::mutex neighborReplyStatusMutex; ///< Mutex for thread-safe access to `replyStatus`.
    std::mutex packetQueueMutex; ///< Mutex for thread-safe access to `packetQueuePerIp`.


    std::vector<std::thread> threads; ///< Threads for handling NDP tasks.
    std::atomic<bool> running; ///< Indicates whether the NDP service is active.
    std::mutex threadMutex; ///< Thread mutex for safe access.

    std::condition_variable threadCV; ///< Condition variable for thread synchronization.

protected:
    void handleNeighborSolicitation(const ByteString& targetIp);
    
    void handleRouteSolicitation(const ByteString& targetIp);

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(const ByteString& targetIp, const ByteString& macAddress);

    void startNudTimer(const ByteString& targetIp);

    void onNudTimeout(const ByteString& targetIp);

    bool waitForNeighborReply(const ByteString& targetIp, const std::chrono::milliseconds& timeout);

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
    PacketInfo routeSolicitation(ByteString& currentMac);

    /**
     * @brief Creates an NDP advertisment packet.
     * @param currentMac The sender's MAC address.
     * @param ttl The max hop count limit.
     * @param lifetime The lifetime of the route entry in seconds.
     * @param reachableTime How long it takes to reach the router in milliseconds.
     * @param retransTimer Timer for retransmitting.
     * @return The constructed Route Advertisment packet.
     */
    PacketInfo routeAdvertisment(const ByteString& currentMac, uint8_t ttl, uint16_t lifetime, uint16_t reachableTime, uint16_t retransTimer);
};

} // namespace Protocol

#endif // ARP_H
