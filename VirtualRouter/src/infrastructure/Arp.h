/**
 * @file Arp.h
 * @brief IPv4 ARP cache and address-resolution engine.
 */

// TODO add per-entry timers

#ifndef ARP_H
#define ARP_H

#include <chrono>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <IPAddress.h>
#include <Mac.hpp>
#include <ControlScheduler.h>
#include "configs/registry/interface/ArpRegistry.h"
#include "configs/RegistryReference.hpp"

namespace core { class Global; }
namespace interface { class Interface; }
namespace processing { class PacketBuilder; }
namespace packet { struct ArpHeader; }

class Internal_ArpTest;

namespace infrastructure
{

/**
 * @class Arp
 * @brief IPv4 ARP cache and resolution engine for a single network interface.
 *
 * Manages the ARP cache for one interface: sends ARP requests, processes
 * incoming replies, handles proxy-ARP entries, and queues packets that are
 * waiting for a next-hop MAC to be resolved.
 *
 * The cache is protected by a shared_mutex to allow concurrent reads from the
 * forwarding path while serialising writes on resolution events.
 *
 * @ingroup INFRASTRUCTURE
 */
class Arp
{
public:
    friend class Internal_ArpTest;

    /**
    * @ingroup INFRASTRUCTURE
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
     * @brief Constructor for the ARP class.
     * @param CurrentInterface Reference to the network interface associated with this ARP instance.
     */
    explicit Arp(interface::Interface& CurrentInterface);

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
    void addArpEntry(types::IPv4Address targetIp, types::Mac targetMac, bool proxy = false, bool isStatic = false);

    void removeArpEntry(types::IPv4Address ip, bool isStatic = false);

    /**
     * @brief Expires an arp entry from the arp cache table
     *
     * @param targetIp The targetIp of the resolved arp entry.
     */
    void expireArpEntry(types::IPv4Address ip);

    /**
     * @brief Resolves an IP address and enqueues a packet to send once resolved.
     * 
     * @param targetIp The target IP address to resolve.
     * @param packetToSend The packet to be sent once the IP is resolved.
     */
    void resolveAndSend(types::IPv4Address targetIp, processing::PacketBuilder& packetToSend);

    /**
     * @brief Sends an ARP reply to a specified MAC and IP.
     * @param targetMac The recipient's MAC address.
     * @param targetIp The recipient's IP address.
     */
    void sendReply(types::Mac targetMac, const types::IPv4Address targetIp);

    /**
     * @brief Sends an ARP request for a given IP.
     * @param targetIp The target IP address to resolve.
     */
    void sendRequest(types::IPv4Address targetIp);

    /**
     * @brief Processes a received ARP reply and updates the cache.
     * @param receivedReply The ARP reply header received from a peer.
     */
    void receiveReply(const packet::ArpHeader& receivedReply);

    /**
     * @brief Processes a received ARP request and updated the cache.
     *
     * @params request The arp header containing the request.
     * @params sourceMac The source mac of the router.
     */
    void receiveRequest(const packet::ArpHeader& request, types::Mac sourceMac);

    /**
     * @brief Retrieves the MAC address for a given IP address.
     * @param mac The MAC container pointer.
     * @param ip The IP address to query.
     * @return True if mac was filled, otherwise false.
     */
     bool getMac(uint8_t* out, types::IPv4Address ip);

    /**
     * @brief Shuts down the ARP service, terminating all threads and cleaning up resources.
     */
    void shutdown();

    /**
     * @brief return if this instance of ARP is shutdown.
     */
    bool isShutdown();

private:
    interface::Interface* currentInterface; ///< Pointer to the associated network interface.

    std::unordered_map<types::IPv4Address, ArpCacheEntry> arpCache; ///< ARP cache mapping IPs to MAC addresses and expiration times.
    std::unordered_map<types::IPv4Address, ArpCacheEntry> staticArpCache; ///< Static ARP entries (never expire).
    std::unordered_map<types::IPv4Address, types::Mac> proxyEntries; ///< Proxy ARP entries (IP -> MAC).
    std::deque<types::IPv4Address> insertionOrder; ///< For tracking eviction order if interface cache limit is exceeded.
    std::unordered_set<types::IPv4Address> pendingRequests; ///< Tracks ongoing ARP requests.
    std::unordered_map<types::IPv4Address, std::atomic<bool>> replyStatus; ///< Tracks ARP reply statuses.
    std::unordered_map<types::IPv4Address, std::queue<processing::PacketBuilder>> packetQueuePerIp; ///< Packets waiting for ARP resolution.
    std::unordered_set<types::IPv4Address> pendingIncompletes;
    std::atomic<uint32_t> incompletes = 0;

    std::atomic<bool> running; ///< Indicates whether the ARP service is active.

    config::Reference<config::ArpRegistry> configs; ///< Config object holding ARP configs

protected:
    /**
     * @brief Creates an ARP request packet.
     *
     * @param packet The packet being built.
     * @param currentMac The sender's MAC address.
     * @param ip The sender's IP address.
     * @param targetIp The target IP address.
     */
    void arpRequest(processing::PacketBuilder& packet, types::Mac currentMac, types::IPv4Address sourceIp, types::IPv4Address targetIp);

    /**
     * @brief Creates an ARP reply packet.
     *
     * @param packet The packet being built.
     * @param currentMac The sender's MAC address.
     * @param targetMac The recipient's MAC address.
     * @param ip The sender's IP address.
     * @param targetIp The recipient's IP address.
     */
    void arpReply(processing::PacketBuilder& packet, types::Mac currentMac, types::Mac targetMac, types::IPv4Address sourceIp, types::IPv4Address targetIp);

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(types::IPv4Address targetIp, types::Mac mac);

    /**
     * @brief Waits for an ARP reply for a given IP address within a timeout period.
     * @param targetIp The target IP address.
     */
    void scheduleRequest(types::IPv4Address targetIp, ArpCacheEntry& entry);

    core::Global& global;
};
} // namespace infrastructure

#endif // ARP_H
