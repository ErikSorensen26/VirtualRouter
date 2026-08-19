/**
 * @file Ndp.h
 * @brief IPv6 Neighbor Discovery Protocol (NDP) cache and resolution engine.
 */

#ifndef NDP_H
#define NDP_H

#include <queue>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <IPAddress.h>
#include <Mac.hpp>
#include <ControlScheduler.h>
#include <AtomicHashMap.hpp>

#include "configs/RegistryReference.hpp"
#include "configs/registry/interface/NdpRegistry.h"
#include "interface/configs/InterfaceConfigs.h"
#include "packet/PacketStructure.h"

namespace core { class Global; }
namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

class Internal_NdpTest;

namespace infrastructure
{
uint8_t* calculateEui64(uint8_t* out, const uint8_t* prefix, const uint8_t* mac);


/**
 * @class Ndp
 * @brief IPv6 Neighbor Discovery Protocol cache and resolution engine for a single interface.
 * @ingroup INFRASTRUCTURE
 *
 * Implements RFC 4861/4862 NDP including:
 * - Neighbor cache with NUD state machine (ACTIVE → REACHABLE → STALE → PROBE → UNREACHABLE)
 * - Duplicate Address Detection (DAD) for IPv6 autoconfiguration
 * - SLAAC (Stateless Address Autoconfiguration) via RA processing
 * - Neighbor Unreachability Detection (NUD) probing
 * - RA Guard and prefix-based filtering
 *
 * ## Concurrency Model
 * All cache mutations run on the interface control scheduler. The data-plane neighbor
 * table (`ndpTable`) is an AtomicHashMap readable lock-free from any thread. All
 * timer callbacks, receive handlers, and cache updates execute on the scheduler thread.
 */
class Ndp
{
public:
    friend class Internal_NdpTest;
    friend class interface::Interface; ///< Needs clear() for teardown ordering.

    /**
     * @enum NudState
     * @brief NUD reachability state machine (RFC 4861 §7.3).
     */
    enum class NudState
    {
        INCOMPLETE,   ///< Neighbor actively reachable via recent Tx or Rx.
        REACHABLE,    ///< Neighbor confirmed reachable; timer running.
        STALE,        ///< Reachable timeout expired; no probing yet.
        PROBE,        ///< Actively probing with NS; retry count running.
    };

    /**
     * @brief NDP cache entry holding resolved MAC, reachability state, and timers.
     */
    struct NdpCacheEntry
    {
        std::queue<processing::PacketBuilder> queue;       ///< Packet queue while waiting on resolution.
        types::Mac macAddress;                             ///< Resolved MAC address.
        NudState   state       = NudState::INCOMPLETE;     ///< Current reachability state.
        uint32_t   timerId     = 0;                        ///< Primary state-transition timer ID.
        uint32_t   retryTimerId = 0;                       ///< NS retry timer ID.
        uint8_t    retries     = 0;                        ///< Current retry count.
    };

    /**
     * @brief Per-address Duplicate Address Detection state for one tentative address.
     *
     * Created when DAD starts and consulted on each probe: the address is
     * accepted once @c retires reaches the configured DAD_ATTEMPTS, or
     * abandoned as soon as @c duplicate is set by an inbound advertisement.
     */
    struct DadEntry
    {
        bool duplicate = false; ///< Set when a peer defends the address; ends DAD immediately.
        uint32_t timerId = 0;   ///< Timer for the next probe.
        uint32_t retires = 0;   ///< Probes sent so far; DAD succeeds at DAD_ATTEMPTS.
    };

    /**
     * @brief Constructs NDP bound to an interface.
     *
     * Initialises configuration and data-plane table but does NOT start timers.
     * If routing is enabled, calls initiateNdp() immediately.
     *
     * @param interface Owning network interface.
     */
    explicit Ndp(interface::Interface& interface);

    /**
     * @brief Destructor. Cancels all pending timers and clears the cache.
     */
    ~Ndp();

    /**
     * @brief Loads static neighbor entries and schedules the first RA (if enabled).
     *
     * Must be called on the control scheduler.
     */
    void initiateNdp();

    /**
     * @brief Clears the NDP cache and re-initialises from configuration.
     *
     * Posted to the control scheduler; safe to call from any thread.
     */
    void refresh();

    /**
     * @brief Adds or updates a dynamic NDP cache entry.
     *
     * Creates a REACHABLE entry, updates the data-plane table, starts the reachability
     * timer, and drains any queued packets waiting for this address.
     *
     * @param targetIp  Target IPv6 address.
     * @param targetMac Resolved MAC address.
     * @param proxy Adds the entry as a proxy.
     */
    void addNdpEntry(types::IPv6Address targetIp, types::Mac targetMac, bool proxy = false);

    /**
     * @brief Adds a static NDP cache entry that never expires.
     *
     * @param targetIp The target IPv6 address of the entry.
     * @param targetMac The MAC the address resolves to.
     * @param proxy Adds the entry as a proxy.
     */
    void addStaticNdpEntry(types::IPv6Address targetIp, types::Mac targetMac, bool proxy = false);

    /**
     * @brief Removes a static cache entry by IPv6 address.
     *
     * @param targetIp Address to remove.
     * @param proxy Removes the entry as a proxy.
     */
    void removeStaticNdpEntry(types::IPv6Address targetIp, bool proxy = false);

    /**
     * @brief Adds a proxy NDP entry for an address this router answers on behalf of.
     *
     * @param targetIp The IPv6 address to proxy for.
     * @param targetMac The MAC to advertise in the Neighbor Advertisement.
     *
     * @warning Declared but not implemented; linking a call to this fails.
     *          Use @ref addStaticNdpEntry with @c proxy set instead.
     */
    void addProxyNdpEntry(types::IPv6Address targetIp, types::Mac targetMac);

    /**
     * @brief Removes a proxy NDP entry previously added for @p targetIp.
     *
     * @param targetIp The proxied IPv6 address to stop answering for.
     *
     * @warning Declared but not implemented; linking a call to this fails.
     *          Use @ref removeStaticNdpEntry with @c proxy set instead.
     */
    void removeProxyNdpEntry(types::IPv6Address targetIp);

    /**
     * @brief Resolves an IPv6 address and transmits a queued packet once resolved.
     *
     * If the address is already REACHABLE, sends immediately. Otherwise queues the
     * packet and initiates Neighbor Solicitation. Enforces resolution-count limits.
     *
     * @param targetIp    Destination IPv6 address.
     * @param packetToSend Packet to send when resolution completes.
     */
    void resolveAndSend(types::IPv6Address targetIp, processing::PacketBuilder& packetToSend);

    /**
     * @brief Processes an inbound Neighbor Advertisement.
     *
     * Updates the cache entry, transitions NUD state, drains queued packets,
     * and handles DAD conflict detection.
     *
     * @param receivedNA Parsed ICMPv6 NA header.
     * @param sourceIp   IPv6 source address of the sender.
     */
    void receiveNeighborAdvertisement(const packet::Icmpv6Header& receivedNA, types::IPv6Address sourceIp);

    /**
     * @brief Processes an inbound Neighbor Solicitation.
     *
     * Replies with NA if the target is a locally-owned or proxy address.
     * Handles DAD NS (unspecified source) and NUD refresh.
     *
     * @param nsHeader Parsed ICMPv6 NS header.
     * @param srcIp    IPv6 source address of the solicitor.
     * @param srcMac   MAC address of the solicitor.
     */
    void receiveNeighborSolicitation(const packet::Icmpv6Header& nsHeader, types::IPv6Address srcIp, types::Mac srcMac);

    /**
     * @brief Processes an inbound Router Advertisement.
     *
     * Applies SLAAC prefix autoconfiguration, updates default route lifetime,
     * enforces RA Guard policy, and triggers DAD for new addresses.
     *
     * @param receivedRA Parsed ICMPv6 RA header.
     * @param sourceIp   IPv6 source address of the router.
     * @param srcMac     MAC address of the router.
     */
    void receiveRouteAdvertisement(const packet::Icmpv6Header& receivedRA, types::IPv6Address sourceIp, types::Mac srcMac);

    /**
     * @brief Processes an inbound Redirect message.
     *
     * Installs a new cache entry for the redirected next-hop.
     *
     * @param redirect  Parsed ICMPv6 Redirect header.
     * @param sourceIp  IPv6 source address of the redirecting router.
     */
    void receiveRedirectMessage(const packet::Icmpv6Header& redirect, types::IPv6Address sourceIp);

    /**
     * @brief Looks up a cached MAC address for fast-path forwarding.
     *
     * Lock-free read from the data-plane AtomicHashMap.
     *
     * @param out Output buffer (must be at least 6 bytes).
     * @param ip  IPv6 address to resolve.
     * @return Pointer to @p out on success; nullptr if not in cache.
     */
    uint8_t* getMac(uint8_t* out, types::IPv6Address ip);

    /**
     * @brief Shuts down NDP and cancels all pending timers.
     *
     * Posts the teardown to the control scheduler; safe to call from any thread.
     */
    void shutdown();

    /**
     * @brief Returns true if shutdown() has been called.
     */
    bool isShutdown();

    /**
     * @brief Initiates Duplicate Address Detection for a tentative IPv6 address.
     *
     * @param address Address object to run DAD on (must be in tentative state).
     */
    void duplicateAddressDetection(interface::InterfaceConfigs::IPv6State::IPv6Address& address);

    /**
     * @brief Drives one step of the DAD probe sequence.
     *
     * Sends an anonymous NS, checks for conflicts, and either marks the address
     * as valid or as duplicate. Scheduled recursively via the control scheduler.
     *
     * @param addr Address undergoing DAD.
     */
    void preformDad(interface::InterfaceConfigs::IPv6State::IPv6Address& addr);

    /**
     * @brief Initiates SLAAC by sending a Router Solicitation.
     */
    void initiateSlaac();

    /**
     * @brief Adds or removes a prefix from the SLAAC exclusion list.
     *
     * @param prefix IPv6 prefix to exclude from autoconfiguration.
     * @param remove If true, removes the prefix from the exclusion list.
     */
    void addSlaacExclusionPrefix(types::IPv6Address prefix, bool remove = false);

    /**
     * @brief Adds or removes a MAC address from the RA Guard whitelist.
     *
     * In MAC_WHITELIST mode only RAs from whitelisted MACs are accepted.
     *
     * @param mac    MAC address to whitelist.
     * @param remove If true, removes the MAC from the whitelist.
     */
    void addRaGuardAllowedMac(types::Mac mac, bool remove = false);

    /**
     * @brief Sends a Neighbor Solicitation for the given target address.
     *
     * Initiates the NS/retry cycle if not already pending.
     *
     * @param targetIp Target IPv6 address to solicit.
     * @param entry The NDP cache entry.
     */
    void sendNeighborSolicitation(types::IPv6Address targetIp, NdpCacheEntry& entry);

    /**
     * @brief Sends a unicast Neighbor Advertisement to a specific destination.
     *
     * @param destMac   Destination MAC address.
     * @param targetIp  Destination IPv6 address.
     */
    void sendNeighborAdvertisement(types::Mac destMac, types::IPv6Address targetIp);

    /**
     * @brief Sends an unsolicited Neighbor Advertisement for the interface link-local address.
     *
     * Rate-limited to one per second per advertised IP.
     */
    void sendNeighborAdvertisement();

    /**
     * @brief Sends an IPv6 Router Solicitation toward the given target.
     *
     * @param targetIp Solicited router address (typically all-routers multicast).
     */
    void sendRouteSolicitation(types::IPv6Address targetIp);

    /**
     * @brief Sends a Router Advertisement to the given destination.
     *
     * @param targetMac Destination MAC address.
     * @param targetIp  Destination IPv6 address.
     */
    void sendRouteAdvertisement(types::Mac targetMac, types::IPv6Address targetIp);

    /**
     * @brief Sends an ICMPv6 Redirect informing a host of a better next hop.
     *
     * @param targetIp      Better next-hop IPv6 address.
     * @param destinationIp Destination for which the redirect applies.
     */
    void sendRedirectMessage(types::IPv6Address targetIp, types::IPv6Address destinationIp);

    /**
     * @brief Sends a redirect if the original packet's forwarding path warrants one.
     *
     * @param originalPacket Parsed packet information.
     * @param pkt            Raw packet bytes.
     */
    void sendRedirectIfNeeded(const packet::PacketInfo& originalPacket, const uint8_t* pkt);

private:
    friend class ::Internal_NdpTest;

    interface::Interface& iface;                                          ///< Owning interface.
    config::NdpRegistry& configs;                       ///< Per-interface NDP configuration.

    // Data-plane table (lock-free reads from forwarding path)
    types::AtomicHashMap<types::IPv6Address, types::Mac> ndpTable;        ///< Fast-path MAC lookup table.

    // Control-plane cache (scheduler-serialized)
    std::unordered_map<types::IPv6Address, NdpCacheEntry> ndpCache;       ///< Neighbor cache (dynamic + static).
    uint32_t incompletes = 0;
    uint32_t nuds = 0;

    // DAD state
    std::unordered_map<types::IPv6Address, DadEntry> dadEntries;          ///< DAD entries

    // Static Proxy
    std::unordered_map<types::IPv6Address, types::Mac> proxyTable;      ///< Proxy Entries

    // RA scheduling and rate-limiting
    std::unordered_set<uint32_t> raTimerIds;                              ///< Active RA timer IDs.
    std::unordered_map<uint64_t,
        std::chrono::steady_clock::time_point> raReceivedTimestamps;      ///< Per-source RA receive timestamps (rate-limit).
    std::unordered_map<types::IPv6Address,
        std::chrono::steady_clock::time_point> lastUnsolicitedNaTime;     ///< Last unsolicited NA time per address.

    // Security
    std::unordered_set<types::Mac>       raGuardAllowedMacs;              ///< RA Guard MAC whitelist.
    std::vector<types::IPv6Address>      slaacExclusionPrefixes;          ///< Prefixes excluded from SLAAC autoconfiguration.

    std::unordered_map<types::IPv6Address, uint32_t> defaultRouterTimers; ///< Per-router default-route expiry timer IDs.

    std::atomic<bool> running;                                            ///< True while NDP is operational.

    core::Global&       global;                                           ///< Global configuration and routing state.
    core::ProcessQueue scheduler;                                      ///< Control-plane scheduler; all mutations run here.

protected:

    /**
     * @brief Cancels all pending timers and clears all cache and table state.
     *
     * Must be called on the control scheduler.
     */
    void clear();

    /**
     * @brief Sends all queued packets for a resolved neighbor.
     *
     * @param targetIp   Resolved IPv6 address.
     * @param macAddress Resolved MAC address.
     */
    void sendQueuedPackets(types::IPv6Address targetIp, types::Mac macAddress);

    /**
     * @brief Dispatches a single queued packet to the next build stage once its
     *        destination MAC has been resolved.
     *
     * @param builder The packet waiting for MAC resolution.
     * @param mac     The resolved destination MAC address.
     */
    void sendQueuedPacket(processing::PacketBuilder& builder, types::Mac mac);

    /**
     * @brief Completes a cache entry: sets MAC, resets timers, updates data-plane table.
     *
     * Updates the entry to REACHABLE state, schedules the reachability timeout (or
     * NUD refresh timer), inserts into ndpTable, and drains any queued packets.
     *
     * @param targetIp Entry's IPv6 address.
     * @param entry    Cache entry to update (must already be in ndpCache).
     * @param mac      Newly resolved MAC address.
     */
    void completeNdpEntry(types::IPv6Address targetIp, NdpCacheEntry& entry, types::Mac mac);

    /**
     * @brief Cancels all timers for an entry and removes it from both cache and table.
     *
     * @param targetIp Address to remove.
     * @param proxy Removes the entry as a proxy.
     */
    void removeNdpEntry(types::IPv6Address targetIp, bool proxy = false);

    /**
     * @brief Called when the REACHABLE timer expires; begins NUD probing or transitions to STALE.
     *
     * @param targetIp Neighbor whose reachable timer fired.
     */
    void onReachableTimeout(types::IPv6Address targetIp);

    /**
     * @brief Schedules the next periodic Router Advertisement transmission.
     *
     * Respects RA_INTERVAL and RA_MIN_INTERVAL (jittered if ADVERTISEMENT_INTERVAL is set).
     * Re-schedules itself on each RA send; stops when running is false or RA_SUPPRESS_ALL is set.
     */
    void scheduleNextRA();

    /**
     * @brief Sends one NS and schedules the next retry for a neighbor.
     *
     * For initial resolution the retry interval is NS_INTERVAL. For NUD probes it is
     * NUD_RETRY_INTERVAL. Removes the entry or transitions to UNREACHABLE on exhaustion.
     *
     * @param targetIp Neighbor to solicit.
     * @param entry NDP cache entry.
     */
    void scheduleNeighborSolicitation(types::IPv6Address targetIp, NdpCacheEntry& entry);

    /**
     * @brief Transitions a neighbor entry into NUD PROBE state and begins probing.
     *
     * Enforces the NUD probe limit (NUD_LIMIT). If the limit is reached, queues the
     * probe for later instead of starting immediately.
     *
     * @param targetIp Neighbor to probe.
     * @param entry    Cache entry to transition (must be in ndpCache).
     */
    void startNud(types::IPv6Address targetIp, NdpCacheEntry& entry);

    /**
     * @brief Refreshes the reachable timer for an entry that is still REACHABLE.
     *
     * Transitions to PROBE if the entry is still REACHABLE when the refresh timer fires.
     *
     * @param targetIp Neighbor to refresh.
     */
    void refreshNeighborEntry(types::IPv6Address targetIp);

    /**
     * @brief Returns true if logging is allowed under the configured rate limit.
     */
    bool shouldLog();

    /**
     * @brief Derives the solicited-node multicast address for a target IPv6 address.
     *
     * Constructs ff02::1:ffXX:XXXX from the last 24 bits of @p targetIp (RFC 4861 §5.1.2).
     *
     * @param targetIp Target address.
     * @return Corresponding solicited-node multicast address.
     */
    types::IPv6Address generateMulticastSolicitationAddress(types::IPv6Address targetIp);

    /**
     * @brief Builds a Neighbor Solicitation ICMPv6 message into @p packet.
     *
     * Includes the source link-layer address option if @p currentMac is non-null.
     *
     * @param packet     PacketBuilder to write into.
     * @param targetIp   Target address for the NS.
     * @param currentMac Optional sender MAC (nullptr to omit option, used for DAD).
     */
    void neighborSolicitation(processing::PacketBuilder& packet, types::IPv6Address targetIp, uint64_t* currentMac);

    /**
     * @brief Builds a Neighbor Advertisement ICMPv6 message into @p packet.
     *
     * Sets S/O flags as appropriate. If @p targetIp is null, uses the interface link-local.
     *
     * @param packet     PacketBuilder to write into.
     * @param currentMac Source MAC for the NA target link-layer option.
     * @param targetIp   Address being advertised (null → interface link-local).
     */
    void neighborAdvertisement(processing::PacketBuilder& packet, types::Mac currentMac, types::IPv6Address* targetIp);

    /**
     * @brief Builds a Router Solicitation ICMPv6 message into @p packet.
     *
     * @param packet     PacketBuilder to write into.
     * @param currentMac Source MAC for the source link-layer option.
     */
    void routeSolicitation(processing::PacketBuilder& packet, types::Mac currentMac);

    /**
     * @brief Builds a Router Advertisement ICMPv6 message into @p packet.
     *
     * Includes prefix information options (if AUTOCONFIG_PREFIX is set), MTU option
     * (unless RA_MTU_SUPPRESS is set), and the source link-layer address option.
     *
     * @param packet     PacketBuilder to write into.
     * @param currentMac Source MAC for the source link-layer option.
     */
    void routeAdvertisement(processing::PacketBuilder& packet, types::Mac currentMac);
};

} // namespace infrastructure

#endif // NDP_H
