// Arp.h

// TODO add per-entry timers

#ifndef ARP_H
#define ARP_H

#include <queue>
#include <Functions.h>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <atomic>

// Forward declarations
class PacketBuilder;
class Interface;
class Global;
class Internal_ArpTest;
struct ArpHeader;

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
    uint8_t macAddress[6]; ///< MAC address associated with the IP.
    uint32_t retries = 0;
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
    void addArpEntry(IPv4Address targetIp, uint64_t targetMac, bool proxy = false, bool isStatic = false);

    void removeArpEntry(IPv4Address ip, bool isStatic = false);

    /**
     * @brief Expires an arp entry from the arp cache table
     *
     * @param targetIp The targetIp of the resolved arp entry.
     */
    void expireArpEntry(IPv4Address ip);

    /**
     * @brief Resolves an IP address and enqueues a packet to send once resolved.
     * 
     * @param targetIp The target IP address to resolve.
     * @param packetToSend The packet to be sent once the IP is resolved.
     */
    void resolveAndSend(IPv4Address targetIp, PacketBuilder& packetToSend);

    /**
     * @brief Sends an ARP reply to a specified MAC and IP.
     * @param targetMac The recipient's MAC address.
     * @param targetIp The recipient's IP address.
     */
    void sendReply(uint64_t targetMac, const IPv4Address targetIp);

    /**
     * @brief Sends an ARP request for a given IP.
     * @param targetIp The target IP address to resolve.
     */
    void sendRequest(IPv4Address targetIp);

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
    void receiveRequest(const ArpHeader& request, uint64_t sourceMac);

    /**
     * @brief Retrieves the MAC address for a given IP address.
     * @param mac The MAC container pointer.
     * @param ip The IP address to query.
     * @return True if mac was filled, otherwise false.
     */
     bool getMac(uint8_t* out, IPv4Address ip);

    /**
     * @brief Shuts down the ARP service, terminating all threads and cleaning up resources.
     */
    void shutdown();

private:
    Interface* currentInterface; ///< Pointer to the associated network interface.

    std::unordered_map<IPv4Address, ArpCacheEntry> arpCache; ///< ARP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<IPv4Address, ArpCacheEntry> staticArpCache; ///< Static ARP entries (never expire).
    std::unordered_map<IPv4Address, uint64_t> proxyEntries; ///< Proxy ARP entries (IP -> MAC).
    std::deque<IPv4Address> insertionOrder; ///< For tracking eviction order if interface cache limit is exceeded.
    std::unordered_set<IPv4Address> pendingRequests; ///< Tracks ongoing ARP requests.
    std::unordered_map<IPv4Address, std::atomic<bool>> replyStatus; ///< Tracks ARP reply statuses.
    std::unordered_map<IPv4Address, std::queue<PacketBuilder>> packetQueuePerIp; ///< Packets waiting for ARP resolution.
    std::unordered_set<IPv4Address> pendingIncompletes;
    std::atomic<uint32_t> incompletes = 0;

    mutable std::shared_mutex arpCacheMutex; ///< Mutex for thread-safe access to the ARP cache.
    std::mutex requestMutex; ///< Mutex for thread-safe access to `pendingRequests`.
    std::mutex replyStatusMutex; ///< Mutex for thread-safe access to `replyStatus`.
    std::mutex packetQueueMutex; ///< Mutex for thread-safe access to `packetQueuePerIp`.

    std::atomic<bool> running; ///< Indicates whether the ARP service is active.

protected:
    /**
     * @brief Creates an ARP request packet.
     *
     * @param packet The packet being built.
     * @param currentMac The sender's MAC address.
     * @param ip The sender's IP address.
     * @param targetIp The target IP address.
     */
    void arpRequest(PacketBuilder& packet, uint64_t currentMac, IPv4Address sourceIp, IPv4Address targetIp);

    /**
     * @brief Creates an ARP reply packet.
     *
     * @param packet The packet being built.
     * @param currentMac The sender's MAC address.
     * @param targetMac The recipient's MAC address.
     * @param ip The sender's IP address.
     * @param targetIp The recipient's IP address.
     */
    void arpReply(PacketBuilder& packet, uint64_t currentMac, uint64_t targetMac, IPv4Address sourceIp, IPv4Address targetIp);

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(IPv4Address targetIp, uint64_t mac);

    /**
     * @brief Waits for an ARP reply for a given IP address within a timeout period.
     * @param targetIp The target IP address.
     */
    void scheduleRequest(IPv4Address targetIp, ArpCacheEntry& entry);

    Global& global;
};

} // namespace Protocol

#endif // ARP_H
