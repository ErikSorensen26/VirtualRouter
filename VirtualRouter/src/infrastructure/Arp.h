/**
 * @file Arp.h
 * @brief IPv4 ARP cache and address-resolution engine.
 */

#ifndef ARP_H
#define ARP_H

#include <chrono>
#include <unordered_map>
#include <AtomicHashMap.hpp>
#include <StableHashMap.hpp>
#include <queue>
#include <IPAddress.h>
#include <Mac.hpp>
#include <ControlScheduler.h>
#include "configs/RegistryReference.hpp"
#include "configs/registry/interface/ArpRegistry.h"
#include "interface/configs/InterfaceType.hpp"
#include "routing/rib/Rib.hpp"

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
        INCOMPLETE, COMPLETE
    };

    /**
    * @struct ArpCacheEntry
    * Represents a single ARP cache entry, including the MAC address and expiration time.
    */
    struct ArpCacheEntry 
    {
        std::queue<processing::PacketBuilder> queue;
        ArpCacheStatus status = ArpCacheStatus::INCOMPLETE;
        uint32_t expireTimerId = 0; ///< Timer id for the lifespan of the arp entry.
        uint32_t requestTimerId = 0; ///< Timer id for arp requests.
        types::Mac macAddress; ///< MAC address associated with the IP.
        uint32_t retries = 0;
        std::chrono::steady_clock::time_point expiryTime; ///< Expiration time for this cache entry.
        std::chrono::steady_clock::time_point renewalTime; ///< Renewal time for this cache entry.
    };

    /**
     * @brief Constructor for the ARP class.
     * @param CurrentInterface Reference to the network interface associated with this ARP instance.
     */
    explicit Arp(interface::Interface& interface);

    /**
     * @brief Clears the ARP cache and re-initialises static entries from VRF config.
     *
     * Posted to the control scheduler, so it is safe to call from any thread.
     */
    void refresh();

    /**
     * @brief Destructor for the ARP class.
     * Ensures a clean shutdown of threads and resources.
     */
    ~Arp();

    /**
     * @brief Adds a static arp entry to the arp cache table
     *
     * @parap targetIp The target IP of the resolved arp entry.
     * @param mac The MAC of the resolved arp entry.
     */
    void addStaticArpEntry(types::IPv4Address targetIp, types::Mac targetMac);

    /**
     * @brief Removes a previously installed static ARP entry from the cache and data-plane table.
     *
     * @param ip The IPv4 address whose static entry should be removed.
     */
    void removeStaticArpEntry(types::IPv4Address ip);

    /**
     * @brief Resolves an IP address and enqueues a packet to send once resolved.
     * 
     * @param targetIp The target IP address to resolve.
     * @param packetToSend The packet to be sent once the IP is resolved.
     */
    void resolveAndSend(types::IPv4Address targetIp, processing::PacketBuilder& packetToSend);

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

    void updateProxyEntry(uint32_t watcherId, const core::RibEntry<uint32_t>* bestRoute);

private:
    interface::Interface& iface; ///< Pointer to the associated network interface.

    config::Reference<config::ArpRegistry> configs; ///< Per-interface ARP configuration (timeout, probe interval, etc.).
    types::StableHashMap<types::IPv4Address, ArpCacheEntry> arpCache; ///< ARP cache mapping IPs to MAC addresses and expiration times.
    types::AtomicHashMap<types::IPv4Address, types::Mac> arpTable; ///< Dataplane ARP table mapping IPs to MAC.

    types::AtomicHashMap<types::IPv4Address, const core::RibEntry<uint32_t>*> proxyEntries; ///< Proxy ARP entries (IP -> interfaceKey).
    std::unordered_map<uint32_t, types::IPv4Address> proxyWatcherIds; ///< Proxy ARP entries (IP -> interfaceKey).

    std::deque<types::IPv4Address> insertionOrder; ///< For tracking eviction order if interface cache limit is exceeded.
    uint32_t incompletes = 0;

    std::atomic<bool> running; ///< Indicates whether the ARP service is active.

protected:

    /**
     * @brief Loads static ARP entries from VRF config and marks the instance as running.
     *
     * Must be called on the control scheduler.
     */
    void initiateArp();

    /**
     * @brief Cancels all pending timers and proxy watchers, then clears both the
     *        control-plane cache and the data-plane ARP table.
     *
     * Must be called on the control scheduler.
     */
    void clear();


    /**
     * @brief Completes an incomplete arp entry to the arp cache table
     *
     * @parap targetIp The target IP of the resolved arp entry.
     * @param mac The MAC of the resolved arp entry.
     */
    void completeArpEntry(std::pair<const types::IPv4Address, ArpCacheEntry>& entry, types::Mac targetMac);

    /**
     * @brief Cancels all timers for an entry and removes it from both the cache
     *        and the data-plane ARP table.
     *
     * @param ip The IPv4 address to remove.
     */
    void removeArpEntry(types::IPv4Address ip);

    /**
     * @brief Installs a proxy-ARP entry and registers a RIB route watcher so the
     *        entry is kept in sync as the best route changes.
     *
     * @param targetIp    The IP address to proxy for.
     * @param proxyEntry  Current best RIB route for that address.
     */
    void addProxyEntry(types::IPv4Address targetIp, const core::RibEntry<uint32_t>* proxyEntry);

    /**
     * @brief Removes a proxy-ARP entry and cancels the associated RIB route watcher.
     *
     * @param addr The proxy IP address to remove.
     */
    void removeProxyEntry(types::IPv4Address addr);

    /**
     * @brief Sends a unicast ARP probe to refresh a COMPLETE cache entry before it expires.
     *
     * Called by the renewal timer (at 80% of TIMEOUT). If a reply arrives, the entry
     * timers are reset via completeArpEntry(). If no reply arrives within PROBE_COUNT
     * probes the entry is removed.
     *
     * @param targetIp The IP address whose entry should be probed.
     */
    void renewArpEntry(types::IPv4Address targetIp);

    /**
     * @brief Sends an ARP reply to a specified MAC and IP.
     * @param targetMac The recipient's MAC address.
     * @param targetIp The recipient's IP address.
     */
    void sendReply(types::Mac targetMac, types::IPv4Address targetIp);

    /**
     * @brief Sends an ARP request for a given IP.
     * @param targetIp The target IP address to resolve.
     * @param cache The cahced ARP entry for this address.
     */
    void sendRequest(types::IPv4Address targetIp, ArpCacheEntry& cache);


    /**
     * @brief Builds and sends an ARP request via the Ethernet stack.
     *
     * Reserves an Ethernet frame (via `ethernet::reserve`), fills the ARP payload,
     * then calls `ethernet::build` to set the Ethernet header and enqueue the frame.
     * The source MAC is read from the interface; the Ethernet destination is broadcast.
     *
     * @param packet   PacketBuilder to write into.
     * @param sourceIp The sender's IPv4 address.
     * @param targetIp The target IPv4 address to resolve.
     */
    void arpRequest(processing::PacketBuilder& packet, types::IPv4Address sourceIp, types::IPv4Address targetIp);

    /**
     * @brief Builds and sends an ARP reply via the Ethernet stack.
     *
     * Reserves an Ethernet frame (via `ethernet::reserve`), fills the ARP payload,
     * then calls `ethernet::build` to set the Ethernet header and enqueue the frame.
     * The source MAC is read from the interface.
     *
     * @param packet    PacketBuilder to write into.
     * @param targetMac The recipient's MAC address.
     * @param sourceIp  The sender's IPv4 address.
     * @param targetIp  The recipient's IPv4 address.
     */
    void arpReply(processing::PacketBuilder& packet, types::Mac targetMac, types::IPv4Address sourceIp, types::IPv4Address targetIp);

    /**
     * @brief Processes queued packets for a resolved IP address and sends them to the resolved MAC address.
     * @param targetIp The resolved IP address.
     * @param macAddress The associated MAC address.
     */
    void processQueuedPackets(types::IPv4Address targetIp, types::Mac mac);

    /**
     * @brief Dispatches a single queued packet to the next build stage once its
     *        destination MAC has been resolved.
     *
     * @param builder The packet waiting for MAC resolution.
     * @param mac     The resolved destination MAC address.
     */
    void sendQueuedPacket(processing::PacketBuilder& builder, types::Mac mac);

    /**
     * @brief Sends an ARP request immediately and schedules a retry timer.
     *
     * For INCOMPLETE entries the retry interval is 1 second and the retry limit is
     * IP_ARP_INCOMPLETE_RETRY. For COMPLETE entries (renewal probes) the interval is
     * PROBE_INTERVAL and the limit is PROBE_COUNT. Removes the entry on exhaustion.
     *
     * @param targetIp The IP address to resolve.
     * @param entry    The cache entry for that address.
     */
    void scheduleRequest(types::IPv4Address targetIp, ArpCacheEntry& entry);

    core::Global& global;           ///< Global configuration and routing-enabled flag.
    core::ProcessQueueRef scheduler; ///< Control-plane scheduler; all cache mutations run here.
};
} // namespace infrastructure

#endif // ARP_H
