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
 * The data-plane table (`arpTable`) is an `AtomicHashMap` readable lock-free
 * from any thread. The control-plane cache (`arpCache`) is only accessed on
 * the interface control scheduler, so no mutex is required for either path.
 *
 * @ingroup INFRASTRUCTURE
 */
class Arp
{
public:
    friend class ::Internal_ArpTest;

    /**
    * @ingroup INFRASTRUCTURE
    * @enum ArpCacheStatus
    * @brief Represents different ARP cache states
    */
    enum class ArpCacheStatus
    {
        INCOMPLETE, COMPLETE
    };

    /**
    * @struct ArpCacheEntry
    * @brief Represents a single ARP cache entry, including the MAC address and expiration time.
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
     * @brief Constructs the ARP instance for one interface.
     * @param interface Interface this ARP instance resolves on; must outlive it.
     */
    explicit Arp(interface::Interface& interface);

    /**
     * @brief Destructor for the ARP class.
     * Ensures a clean shutdown of threads and resources.
     */
    ~Arp();

    /**
     * @brief Clears the ARP cache and re-initialises static entries from VRF config.
     *
     * Posted to the control scheduler, so it is safe to call from any thread.
     */
    void refresh();

    /**
     * @brief Adds an arp entry to the arp cache table
     *
     * @param targetIp Target IPv4 address.
     * @param targetMac Target MAC address.
     * @param proxy Adds the entry as a proxy entry.
     */
    void addArpEntry(types::IPv4Address targetIp, types::Mac targetMac, bool proxy = false);

    /**
     * @brief Adds a static arp entry to the arp cache table
     *
     * @param targetIp The target IP of the resolved arp entry.
     * @param targetMac The MAC of the resolved arp entry.
     * @param proxy Adds the entry as a proxy entry.
     */
    void addStaticArpEntry(types::IPv4Address targetIp, types::Mac targetMac, bool proxy = false);

    /**
     * @brief Removes a previously installed static ARP entry from the cache and data-plane table.
     *
     * @param ip The IPv4 address whose static entry should be removed.
     * @param proxy Removes the entry as a proxy entry.
     */
    void removeStaticArpEntry(types::IPv4Address ip, bool proxy = false);

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
     * @param request The arp header containing the request.
     * @param sourceMac The source mac of the router.
     */
    void receiveRequest(const packet::ArpHeader& request, types::Mac sourceMac);

    /**
     * @brief Retrieves the MAC address for a given IP address.
     * @param[out] out Buffer receiving the resolved MAC; written only on success.
     * @param ip The IP address to query.
     * @return True if @p out was filled, otherwise false.
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
    interface::Interface& iface; ///< Pointer to the associated network interface.

    config::ArpRegistry& configs; ///< Per-interface ARP configuration (timeout, probe interval, etc.).
    types::StableHashMap<types::IPv4Address, ArpCacheEntry> arpCache; ///< ARP cache mapping IPs to MAC addresses and expiration times.
    types::AtomicHashMap<types::IPv4Address, types::Mac> arpTable; ///< Dataplane ARP table mapping IPs to MAC.
    std::unordered_map<types::IPv4Address, types::Mac> proxyTable; ///< Proxy table for static entries.

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
     * @param entry The cache entry being resolved, keyed by its target IP.
     * @param targetMac The MAC of the resolved arp entry.
     */
    void completeArpEntry(std::pair<const types::IPv4Address, ArpCacheEntry>& entry, types::Mac targetMac);

    /**
     * @brief Cancels all timers for an entry and removes it from both the cache
     *        and the data-plane ARP table.
     *
     * @param ip The IPv4 address to remove.
     */
    void removeArpEntry(types::IPv4Address ip, bool proxy = false);

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

public:
    /**
     * @brief Sends an ARP reply to a specified MAC and IP.
     * @param targetMac The recipient's MAC address.
     * @param targetIp The recipient's IP address.
     */
    void sendReply(types::Mac targetMac, types::IPv4Address targetIp);
protected:

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
     * @param mac The associated MAC address.
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
    core::ProcessQueue scheduler; ///< Control-plane scheduler; all cache mutations run here.
};
} // namespace infrastructure

#endif // ARP_H
