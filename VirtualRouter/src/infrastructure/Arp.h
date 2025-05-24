// Arp.h

// TODO add per-entry timers

#ifndef ARP_H
#define ARP_H

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

// Forward declarations
class Interface;
class Global;
class Internal_ArpTest;

/**
 * @namespace Protocol
 * Contains network protocol implementations.
 */
namespace Protocol 
{

/**
 * @enum ArpCacheStatus
 * Represents different ARP cache states 
 */
enum class ArpCacheStatus
{
    INCOMPLETE, COMPLETE, STALE
};

/**
 * @struct ArpCacheEntry
 * Represents a single ARP cache entry, including the MAC address and expiration time.
 */
struct ArpCacheEntry 
{
    ArpCacheStatus status = ArpCacheStatus::COMPLETE;
    uint32_t timerId = 0; ///< Timer id for the lifespan of the arp entry.
    ByteString macAddress; ///< MAC address associated with the IP.
    int retries = 0;
    std::chrono::steady_clock::time_point expiryTime; ///< Expiration time for this cache entry.
};

/**
 * @class Arp
 * Handles ARP functionality, including cache management, ARP request/reply handling, and packet resolution.
 */
class Arp 
{
public:
    friend class ::Internal_ArpTest;

    /**
     * @struct Configs.
     * @brief holds configurations for ARP.
     */
    struct Configs
    {
        std::atomic<bool> authorized = false;
        std::atomic<bool> packetPriority = false; //TODO

        std::atomic<uint8_t> probeInterval = 5;
        std::atomic<uint8_t> probeCount = 3;

        std::atomic<uint32_t> loggingThreshold; //TODO
        std::atomic<uint32_t> timeout = 14400;
    } configs;

    /**
     * @brief Constructor for the ARP class.
     * @param CurrentInterface Reference to the network interface associated with this ARP instance.
     */
    explicit Arp(Interface& CurrentInterface);

    void initiateArp();

    /**
     * @brief Destructor for the ARP class.
     * Ensures a clean shutdown of threads and resources.
     */
    ~Arp();

    /**
     * @brief Adds an arp entry to the arp cache table
     *
     * @parap targetIp The target IP of the resolved arp entry.
     * @param mac The MAC of the resolved arp entry.
     */
    void addArpEntry(const ByteString& targetIp, const ByteString& targetMac, bool proxy = false, bool isStatic = false);

    void removeArpEntry(const ByteString& ip, bool isStatic = false);

    /**
     * @brief Expires an arp entry from the arp cache table
     *
     * @param targetIp The targetIp of the resolved arp entry.
     */
    void expireArpEntry(const ByteString& ip);

    /**
     * @brief Resolves an IP address and enqueues a packet to send once resolved.
     * 
     * @param targetIp The target IP address to resolve.
     * @param packetToSend The packet to be sent once the IP is resolved.
     */
    void resolveAndSend(const ByteString& targetIp, PacketInfo& packetToSend);

    /**
     * @brief Sends an ARP reply to a specified MAC and IP.
     * @param targetMac The recipient's MAC address.
     * @param targetIp The recipient's IP address.
     */
    void sendReply(const ByteString& targetMac, const ByteString& targetIp);

    /**
     * @brief Sends an ARP request for a given IP.
     * @param targetIp The target IP address to resolve.
     */
    void sendRequest(const ByteString& targetIp);

    /**
     * @brief Processes a received ARP reply and updates the cache.
     * @param receivedReply The ARP reply header received from a peer.
     */
    void receiveReply(const ArpHeader& receivedReply);

    /**
     * @brief Processes a received ARP request and updated the cache.
     *
     * @params request The arp header containing the request.
     * @params sourceMac The source mac of the router.
     */
    void receiveRequest(const ArpHeader& request, const ByteString& sourceMac);

    /**
     * @brief Retrieves the MAC address for a given IP address.
     * @param ip The IP address to query.
     * @return The associated MAC address if found, or an empty string otherwise.
     */
    ByteString* getMac(const ByteString& ip);

    /**
     * @brief Shuts down the ARP service, terminating all threads and cleaning up resources.
     */
    void shutdown();

private:
    Interface* currentInterface; ///< Pointer to the associated network interface.

    std::unordered_map<ByteString, ArpCacheEntry> arpCache; ///< ARP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<ByteString, ArpCacheEntry> staticArpCache; ///< Static ARP entries (never expire).
    std::unordered_map<ByteString, ByteString> proxyEntries; ///< Proxy ARP entries (IP -> MAC).
    std::deque<ByteString> insertionOrder; ///< For tracking eviction order if interface cache limit is exceeded.
    std::unordered_set<ByteString> pendingRequests; ///< Tracks ongoing ARP requests.
    std::unordered_map<ByteString, std::atomic<bool>> replyStatus; ///< Tracks ARP reply statuses.
    std::unordered_map<ByteString, std::queue<PacketInfo>> packetQueuePerIp; ///< Packets waiting for ARP resolution.
    std::unordered_set<ByteString> pendingIncompletes;
    std::atomic<uint32_t> incompletes = 0;

    mutable std::shared_mutex arpCacheMutex; ///< Mutex for thread-safe access to the ARP cache.
    std::mutex requestMutex; ///< Mutex for thread-safe access to `pendingRequests`.
    std::mutex replyStatusMutex; ///< Mutex for thread-safe access to `replyStatus`.
    std::mutex packetQueueMutex; ///< Mutex for thread-safe access to `packetQueuePerIp`.

    std::atomic<bool> running; ///< Indicates whether the ARP service is active.

protected:
    /**
     * @brief Creates an ARP request packet.
     * @param currentMac The sender's MAC address.
     * @param ip The sender's IP address.
     * @param targetIp The target IP address.
     * @return The constructed ARP request packet.
     */
    PacketInfo arpRequest(const ByteString& currentMac, const ByteString& ip, const ByteString targetIp);

    /**
     * @brief Creates an ARP reply packet.
     * @param currentMac The sender's MAC address.
     * @param targetMac The recipient's MAC address.
     * @param ip The sender's IP address.
     * @param targetIp The recipient's IP address.
     * @return The constructed ARP reply packet.
     */
    PacketInfo arpReply(const ByteString& currentMac, const ByteString& targetMac, const ByteString& ip, const ByteString& targetIp);

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(const ByteString& targetIp, const ByteString& macAddress);

    /**
     * @brief Waits for an ARP reply for a given IP address within a timeout period.
     * @param targetIp The target IP address.
     */
    void scheduleRequest(const ByteString& targetIp, ArpCacheEntry& entry);

    Global& global;
};

} // namespace Protocol

#endif // ARP_H
