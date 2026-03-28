/**
 * @file Ndp.h
 * @brief IPv6 Neighbor Discovery Protocol (NDP) and IPv4 Address Resolution Protocol (ARP) cache and resolution.
 */

#ifndef NDP_H
#define NDP_H

#include <queue>
#include <atomic>
#include <shared_mutex>
#include <IPAddress.h>
#include <Mac.hpp>

#include "interface/configs/InterfaceConfigs.h"
#include "packet/PacketStructure.h"

// TODO: Have the ability to insert entries when shutdown

class Internal_NdpTest;

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

namespace infrastructure
{

/**
 * @brief IPv6 Neighbor Discovery and IPv4 ARP cache with DAD, RA processing, and NUD.
 * @ingroup INFRASTRUCTURE
 *
 * Implements NDP for IPv6 (RFC 4861, RFC 4862) and ARP for IPv4, including:
 * - Address cache with reachability state transitions (ACTIVE, REACHABLE, STALE, DELAY, PROBE, UNREACHABLE)
 * - Duplicate Address Detection (DAD) for IPv6 autoconfiguration
 * - SLAAC (Stateless Address Autoconfiguration)
 * - Neighbor Unreachability Detection (NUD) state machine
 * - RA Guard and prefix-based filtering
 *
 * ## Architectural Role
 * Sits at the interface layer, resolving next-hop addresses to MAC addresses before
 * frame transmission. Caches resolved entries and automatically triggers resolution
 * when addresses are unknown. Coordinates with interface configuration for address
 * management and with timer callbacks for state transitions.
 *
 * ## Lifecycle & Ownership
 * Owned by Interface. Constructed with a reference to the owning interface and
 * the global system. Torn down when the interface is deleted. All async operations
 * (DAD, NUD probes, RA schedules) are coordinated via the global timer manager.
 *
 * ## Concurrency Model
 * - Cache access guarded by ndpCacheMutex (std::shared_mutex for read-heavy workloads)
 * - Each pending operation (DAD, NS retries) guarded by separate mutexes
 * - Atomic counters track in-flight resolution and probes for backpressure
 * - All state transitions driven by timer callbacks on the global timer thread
 *
 * ## Fast Path vs. Slow Path
 * - Fast path: Cache hit on resolveAndSend() — immediate MAC lookup and frame transmission
 * - Slow path: Cache miss — queue packet, initiate NS probe, wait for reply
 *
 * @warning Calling methods after shutdown() may silently drop packets or fail to
 * update cache entries. Always drain queues and ensure no active timers before destruction.
 *
 * @see Interface, InfrastructureIppacket
 */
class Ndp
{
public:
    friend class Internal_NdpTest;

    /**
     * @brief Configuration options for NDP and RA behavior.
     * @ingroup INFRASTRUCTURE
     *
     * Controls SLAAC, DAD, NUD state machine, RA Guard, and IPv6 preferences.
     * Most settings are atomic to allow runtime changes without locking.
     */
    struct Configs
    {
        /**
         * @enum Preference
         * @brief Router preference for default route selection (RFC 4191).
         */
        enum class Preference { HIGH, MEDIUM, LOW };

        /**
         * @enum RaGuardMode
         * @brief RA Guard protection level against unauthorized router announcements.
         */
        enum class RaGuardMode : uint8_t { BLOCK_ALL, TRUSTED, MAC_WHITELIST};

        // IPv6 SLAAC and RA handling
        std::atomic<bool> slaacEnabled = false;                 ///< Enable SLAAC prefix autoconfiguration.
        std::atomic<bool> advertisementInterval = false;        ///< Control RA timing.
        std::atomic<bool> autoConfigDefaultRoute = false;       ///< Auto-learn default route from RA.
        std::atomic<bool> autoConfigPrefix = false;             ///< Auto-learn prefixes from RA.
        std::atomic<bool> destinationGuard = false;             ///< Block RA-advertised on-link destinations.
        std::atomic<bool> managedConfigFlag = false;            ///< M flag handling (DHCPv6 managed).
        std::atomic<bool> otherConfigFlag = false;              ///< O flag handling (DHCPv6 other info).

        // NUD and ND processing flags
        std::atomic<bool> naGlean = false;                      ///< Learn from unsolicited NA.
        std::atomic<bool> nudIgp = false;                       ///< Use NUD for IGP reachability tracking.
        std::atomic<bool> mtuSuppress = false;                  ///< Ignore MTU Option in RA.
        std::atomic<bool> suppressRA = false;                   ///< Don't send RA.
        std::atomic<bool> suppressNA = false;                   ///< Don't send NA (e.g., on anycast).
        std::atomic<bool> raSuppressAll = false;                ///< Block all RA reception.
        std::atomic<bool> raHopLimitUnspecified = false;        ///< Ignore Hop Limit Option in RA.
        std::atomic<bool> redirects = false;                    ///< Accept redirect messages.

        // Timers and thresholds (milliseconds unless noted)
        std::atomic<uint16_t> dadAttempts = 1;                  ///< DAD probe count.
        std::atomic<uint16_t> raLifetime = 1800;                ///< RA lifetime in seconds.
        std::atomic<uint16_t> raPreferredLifetime = 900;        ///< Prefix preferred lifetime in seconds.
        std::atomic<uint16_t> routerLifetime = 1800;            ///< Default router lifetime in seconds.

        std::atomic<uint32_t> nsInterval = 1000;                ///< NS probe interval (ms).
        std::atomic<uint32_t> raRateLimit = 5;                  ///< Max RA flood events per window.

        // Core global configuration cache
        std::atomic<bool> refresh;                              ///< Force config refresh flag.
        std::atomic<uint16_t> loggingRate;                      ///< Logging rate limit.
        std::atomic<uint16_t> cacheExpire;                      ///< Cache entry expiry time (seconds).
        std::atomic<uint16_t> dadTime;                          ///< DAD timeout (ms).
        std::atomic<uint32_t> reachableTime;                    ///< Reachable timeout (ms).
        std::atomic<uint32_t> interfaceLimit;                   ///< Max entries per interface.

        bool refreshLocal = false;                              ///< Local refresh flag.
        bool cacheExpireLocal = false;
        bool dadTimeLocal = false;
        bool interfaceLimitLocal = false;
        bool reachableTimeLocal = false;
        bool loggingRateLocal = false;

        std::atomic<Preference> preference = Preference::MEDIUM;           ///< RA preference level.
        std::atomic<RaGuardMode> raGuardMode = RaGuardMode::BLOCK_ALL;    ///< RA Guard mode.

        std::shared_mutex configMutex;                          ///< Synchronizes config updates.

        // NUD state machine timing (milliseconds)
        uint8_t nudBase = 3;                                    ///< NUD probe base count.
        uint16_t nudBaseInterval = 1000;                        ///< Interval between NUD probes.
        uint16_t nudBaseAttempts = 3;                           ///< Attempts before unreachable.
        uint16_t nudFinalWait = 60000;                          ///< Final wait before aging out.

        bool raIntervalMS = true;                               ///< RA interval in milliseconds (vs seconds).
        uint32_t raInterval = 600000;                           ///< RA transmission interval.
        uint32_t raIntervalMin = 3000;                          ///< Min RA interval.

    } configs;

    /**
     * @enum NudState
     * @brief NUD reachability state machine (RFC 4861 section 7.3).
     */
    enum class NudState
    {
        ACTIVE,        ///< Neighbor actively reachable via recent Tx or Rx.
        REACHABLE,     ///< Neighbor reachable; timer running.
        STALE,         ///< Reachable timeout expired; no probing yet.
        DELAY,         ///< Stale neighbor; waiting before PROBE.
        PROBE,         ///< Actively probing with NS (attempts pending).
        UNREACHABLE    ///< Probe attempts exhausted.
    };

    /**
     * @brief NDP cache entry with MAC address, state, and timers.
     * @ingroup INFRASTRUCTURE
     *
     * Holds resolved MAC address, current reachability state, and associated timers
     * for NUD transitions and DAD reschedules.
     */
    struct NdpCacheEntry
    {
        types::Mac macAddress;                                  ///< Resolved MAC address.
        std::chrono::steady_clock::time_point expiryTime;       ///< Entry expiration timestamp.
        NudState state = NudState::ACTIVE;                      ///< Current reachability state.
        uint32_t timerId = 0;                                   ///< NUD state transition timer ID.
        uint8_t nudGroup = 1;                                   ///< NUD grouping for batch processing.
        uint32_t nudRetryTimerId = 0;                           ///< NUD probe retry timer ID.
    };

    /**
     * @brief Constructs NDP instance bound to an interface.
     *
     * Initializes cache structures and configuration but does NOT start timers.
     * Call initializeNdp() after construction to begin DAD, SLAAC, or RA processing.
     *
     * @param CurrentInterface Reference to the owning interface.
     *
     * @note No async operations occur until initializeNdp() is called.
     */
    explicit Ndp(interface::Interface& CurrentInterface);

    /**
     * @brief Initializes NDP timers and starts DAD/SLAAC if configured.
     *
     * Called after construction to activate NDP state machine and start
     * RA solicitation, SLAAC, or DAD as appropriate for the interface config.
     */
    void initializeNdp();

    /**
     * @brief Destructs NDP and cancels all pending timers.
     *
     * Ensures all active timers (DAD, NUD, RA scheduling) are cancelled and
     * resources are cleaned up. Safe to call even if initializeNdp() was not invoked.
     */
    ~Ndp();

    /**
     * @brief Adds or updates an NDP cache entry.
     *
     * Resolves a target IP to its MAC address in the cache. If an entry
     * already exists, updates it and resets the expiry timer. Triggers
     * transmission of any queued packets for this IP.
     *
     * @param targetIp Target IPv6 address to resolve.
     * @param targetMac Resolved MAC address.
     * @param proxy If true, adds to proxy cache (outgoing only). Default false.
     * @param isStatic If true, entry does not expire. Default false.
     *
     * @see resolveAndSend
     */
    void addNdpEntry(types::IPv6Address targetIp, types::Mac targetMac, bool proxy = false, bool isStatic = false);

    /**
     * @brief Resolves an IP address and sends queued packet, or queues if unresolved.
     *
     * If the IP is in cache, immediately transmits the packet with the cached MAC.
     * If not in cache, queues the packet and initiates NS probe. Dropped if resolution
     * queue exceeds system limits.
     *
     * @param targetIp Target IPv6 address to resolve.
     * @param packetToSend Packet to transmit (modified with destination MAC on success).
     *
     * @warning Caller is responsible for ensuring packetToSend is valid for the
     * duration of queueing (may be sent later after cache resolution).
     *
     * @see addNdpEntry
     */
    void resolveAndSend(types::IPv6Address targetIp, processing::PacketBuilder& packetToSend);

    /**
     * @brief Sends an IPv6 Neighbor Solicitation probe.
     *
     * Constructs and transmits NS targeting the given IP. Used during resolution,
     * NUD probing, and DAD. The multicast solicitation address is derived from
     * the target according to RFC 4861.
     *
     * @param targetIp Target IPv6 address to probe.
     */
    void sendNeighborSolicitation(types::IPv6Address targetIp);

    /**
     * @brief Sends a Neighbor Advertisement with optional S and O flags.
     *
     * @param currentMac Source MAC address for this NA.
     * @param targetIp Destination IPv6 address for the NA (unicast to solicitor if provided).
     */
    void sendNeighborAdvertisement(types::Mac currentMac, types::IPv6Address targetIp);

    /**
     * @brief Sends an unsolicited Neighbor Advertisement for all local addresses.
     *
     * Triggered after interface up or address addition to notify neighbors
     * of reachability.
     */
    void sendNeighborAdvertisement();

    /**
     * @brief Sends an IPv6 Router Solicitation to solicit RA from routers.
     *
     * @param targetIp Solicited router IPv6 address (typically all-routers multicast).
     */
    void sendRouteSolicitation(types::IPv6Address targetIp);

    /**
     * @brief Sends a Router Advertisement to the given target.
     *
     * Transmits RA with configured prefixes, default route lifetime, and flags.
     * Used for responding to RS or periodic unsolicited RA transmission.
     *
     * @param targetMac Destination MAC address.
     * @param targetIp Destination IPv6 address.
     */
    void sendRouteAdvertisement(types::Mac targetMac, types::IPv6Address targetIp);

    /**
     * @brief Sends an ICMPv6 Redirect message to a host.
     *
     * Informs a host that a better route exists through a different gateway.
     * Subject to redirect configuration and rate limiting.
     *
     * @param targetIp The better next-hop address to redirect to.
     * @param destinationIp The destination IP for which the redirect applies.
     */
    void sendRedirectMessage(types::IPv6Address targetIp, types::IPv6Address destinationIp);

    /**
     * @brief Sends a redirect if the original packet forwarding warrants one.
     *
     * Examines the packet and interface configuration to determine if a redirect
     * should be sent back to the sender. Rate-limited and subject to RA Guard.
     *
     * @param originalPacket Parsed packet information.
     * @param pkt Raw packet bytes (for optional inspection).
     */
    void sendRedirectIfNeeded(const packet::PacketInfo& originalPacket, const uint8_t* pkt);

    /**
     * @brief Processes an inbound Neighbor Advertisement.
     *
     * Updates cache with MAC if the NA is valid, transitions NUD state, and
     * processes queued packets if this was a solicited NA. Checks for duplicate
     * address conflicts.
     *
     * @param receivedNA Parsed NA ICMPv6 header.
     * @param sourceIp IPv6 address of the sender.
     */
    void receiveNeighborAdvertisement(const packet::Icmpv6Header& receivedNA, types::IPv6Address sourceIp);

    /**
     * @brief Processes an inbound Neighbor Solicitation.
     *
     * Responds to NS if the target address is local, updates cache if solicitor
     * is in cache, and handles DAD conflict detection.
     *
     * @param nsHeader Parsed NS ICMPv6 header.
     * @param srcIp IPv6 source address of the sender.
     * @param srcMac MAC address of the sender.
     */
    void receiveNeighborSolicitation(const packet::Icmpv6Header& nsHeader, types::IPv6Address srcIp, types::Mac srcMac);

    /**
     * @brief Processes an inbound Router Advertisement.
     *
     * Updates default route, applies SLAAC prefixes, schedules DAD if needed,
     * and enforces RA Guard policy. Subject to M and O flag processing.
     *
     * @param receivedRA Parsed RA ICMPv6 header.
     * @param sourceIp IPv6 source address of the router.
     * @param srcMac MAC address of the router.
     */
    void receiveRouteAdvertisement(const packet::Icmpv6Header& receivedRA, types::IPv6Address sourceIp, types::Mac srcMac);

    /**
     * @brief Processes an inbound Redirect message.
     *
     * Updates cache and routing table with the redirected next-hop if valid.
     * Subject to redirect configuration and rate limiting.
     *
     * @param redirect Parsed Redirect ICMPv6 header.
     * @param sourceIp IPv6 source address of the router sending the redirect.
     */
    void receiveRedirectMessage(const packet::Icmpv6Header& redirect, types::IPv6Address sourceIp);

    /**
     * @brief Shuts down NDP and cancels all pending operations.
     *
     * Cancels all timers, clears queued packets, and transitions to offline.
     * Called when the interface is brought down or deleted.
     *
     * @note After shutdown(), only cache reads are safe; new resolutions will fail.
     */
    void shutdown();

    /**
     * @brief Checks if NDP is currently shut down.
     *
     * @return True if shutdown() has been called, false otherwise.
     */
    bool isShutdown();

    /**
     * @brief Initiates Duplicate Address Detection for a configured address.
     *
     * Schedules DAD probes (NS to multicast solicitation address) and updates
     * the address state in the interface config. Addresses must reach TENTATIVE
     * state before DAD completes.
     *
     * @param address IPv6 address undergoing DAD.
     */
    void duplicateAddressDetection(interface::InterfaceConfigs::IPv6State::IPv6Address& address);

    /**
     * @brief Performs DAD probe sequence for an address.
     *
     * Internal method driving the DAD state machine (issues NS, waits for timeout,
     * transitions to PREFERRED if no conflict detected, or flags as DUPLICATE).
     *
     * @param addr Address undergoing DAD.
     */
    void preformDad(interface::InterfaceConfigs::IPv6State::IPv6Address& addr);

    /**
     * @brief Initiates SLAAC prefix autoconfiguration.
     *
     * Starts RS transmission to solicit RA, then processes received prefixes to
     * generate link-local and global addresses automatically.
     */
    void initiateSlaac();

    /**
     * @brief Adds or removes a prefix from the SLAAC exclusion list.
     *
     * Excluded prefixes are not autoconfigured when received in RA. Useful for
     * blocking known unwanted prefixes.
     *
     * @param prefix IPv6 prefix to exclude.
     * @param remove If true, removes from exclusion list. Default false (add).
     */
    void addSlaacExclusionPrefix(types::IPv6Address prefix, bool remove = false);

    /**
     * @brief Adds or removes a MAC from the RA Guard whitelist.
     *
     * In MAC_WHITELIST mode, only RAs from whitelisted MACs are accepted.
     *
     * @param mac MAC address to whitelist/blacklist.
     * @param remove If true, removes from whitelist. Default false (add).
     */
    void addRaGuardAllowedMac(types::Mac mac, bool remove = false);

    /**
     * @brief Resolves an IPv6 address to its cached MAC address.
     *
     * @param out Output buffer for MAC address (must be 6 bytes).
     * @param ip IPv6 address to resolve.
     * @return Pointer to the MAC buffer on success, nullptr if not in cache.
     */
    uint8_t* getMac(uint8_t* out, types::IPv6Address ip);

private:
    interface::Interface* currentInterface;                     ///< Owning interface.

    // Caches: insertion order, main, static, and proxy
    std::vector<types::IPv6Address> insertionOrder;
    std::unordered_map<types::IPv6Address, NdpCacheEntry> ndpCache;         ///< Dynamic cache entries.
    std::unordered_map<types::IPv6Address, NdpCacheEntry> staticNdpCache;   ///< Static (never-expire) entries.
    std::unordered_map<types::IPv6Address, uint64_t> proxyEntries;          ///< Proxy entries (outgoing only).

    // Resolution and state tracking
    std::unordered_set<types::IPv6Address> pendingRequests;    ///< IPs awaiting NS response.
    std::unordered_map<types::IPv6Address, bool> neighborReplyStatus;       ///< NA receive status tracking.
    std::unordered_map<types::IPv6Address, bool> routeReplyStatus;          ///< RA receive status tracking.
    std::unordered_map<types::IPv6Address, std::queue<processing::PacketBuilder>> packetQueuePerIp; ///< Queued packets pending resolution.

    // RA Guard and security
    std::unordered_set<types::Mac> raGuardAllowedMacs;           ///< Whitelisted RA sources.

    // Timing and scheduling
    std::unordered_map<types::IPv6Address, std::chrono::steady_clock::time_point> lastUnsolicitedNaTime;
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> raReceivedTimestamps;
    std::unordered_map<types::IPv6Address, uint32_t> nudDelayTimers;
    std::unordered_map<types::IPv6Address, uint32_t> pendingDadReschedules;
    std::atomic<std::chrono::steady_clock::time_point> lastLogWindowStart;
    std::chrono::steady_clock::time_point lastRaReceiveTime;

    // In-flight counters for backpressure
    std::atomic<uint32_t> currentNudProbes = 0;               ///< Active NUD probe count.
    std::atomic<uint32_t> currentResolvingNeighbors = 0;      ///< Neighbors awaiting resolution.
    std::atomic<uint32_t> nfsResolutionCount = 0;             ///< NFS-related resolutions.
    std::unordered_set<types::IPv6Address> queuedNudProbes;   ///< Queued NUD probe operations.
    std::unordered_set<types::IPv6Address> queuedResolution;  ///< Queued resolution operations.

    // SLAAC configuration
    std::vector<types::IPv6Address> slaacExclusionPrefixes;   ///< Prefixes not to autoconfigure.

    // Synchronization
    mutable std::shared_mutex ndpCacheMutex;                  ///< Protects all cache structures.
    std::mutex requestMutex;                                  ///< Protects pendingRequests.
    std::mutex neighborReplyStatusMutex;                      ///< Protects neighborReplyStatus.
    std::mutex packetQueueMutex;                              ///< Protects packetQueuePerIp.

    // Timer management
    std::unordered_set<uint32_t> raTimerIds;                  ///< Active RA timer IDs.
    std::unordered_map<types::IPv6Address, uint32_t> nsRetryTimers;         ///< NS retry timer per IP.
    std::unordered_map<types::IPv6Address, uint8_t> nsRetryCount;           ///< NS retry count per IP.
    std::unordered_map<types::IPv6Address, uint32_t> dadTimers;             ///< DAD timer per IP.

    std::atomic<bool> running;                                ///< NDP service running state.

protected:
    /**
     * @brief Transmits queued packets after successful address resolution.
     *
     * Called after cache lookup succeeds or NA is received. Drains the packet
     * queue for the target IP and transmits each queued packet with the resolved MAC.
     *
     * @param targetIp Resolved IPv6 address.
     * @param macAddress Associated MAC address.
     */
    void processQueuedPackets(types::IPv6Address targetIp, types::Mac macAddress);

    /**
     * @brief Callback invoked when a neighbor transitions from REACHABLE to stale.
     *
     * @param targetIp IPv6 address of the neighbor.
     */
    void onReachableTimeout(types::IPv6Address targetIp);

    /**
     * @brief Schedules the next RA transmission.
     *
     * Called during initialization or when RA timing is updated.
     * Respects raInterval and raIntervalMin configuration.
     */
    void scheduleNextRA();

    /**
     * @brief Schedules NS probe retries for a neighbor.
     *
     * Called when initial NS probe fails or state transitions require more probes.
     *
     * @param targetIp IPv6 address to probe.
     */
    void scheduleNeighborSolicitation(types::IPv6Address targetIp);

    /**
     * @brief Transitions a neighbor entry into NUD PROBE state with retries.
     *
     * Increments probe counters and schedules NS transmission. Called from
     * state machine transitions when REACHABLE expires or STALE entry needs validation.
     *
     * @param targetIp IPv6 address to probe.
     * @param entry NDP cache entry (modified in-place).
     * @param cacheLock Held lock on ndpCacheMutex (required).
     */
    void startNud(types::IPv6Address targetIp, NdpCacheEntry& entry, std::unique_lock<std::shared_mutex>& cacheLock);

    /**
     * @brief Refreshes a neighbor entry's reachable timeout.
     *
     * Called after successful communication to reset the REACHABLE timer
     * and prevent premature transition to STALE.
     *
     * @param targetIp IPv6 address of the neighbor.
     */
    void refreshNeighborEntry(types::IPv6Address targetIp);

    /**
     * @brief Checks if logging is allowed under the current rate limit.
     *
     * @return True if the logging window permits another message.
     */
    bool shouldLog();

    /**
     * @brief Retries NUD probing for a neighbor after probe failure.
     *
     * Increments retry count and reschedules NS. Transitions to UNREACHABLE
     * if retries exhausted.
     *
     * @param targetIp IPv6 address being probed.
     */
    void retryNud(types::IPv6Address targetIp);

    /**
     * @brief Schedules processing of a neighbor entry (e.g., state transition).
     *
     * Used by timer callbacks to queue pending operations. Prevents concurrent
     * modification of cache structures.
     *
     * @param ip IPv6 address of the neighbor.
     */
    void scheduleNeighborEntry(types::IPv6Address ip);

    /**
     * @brief Derives the IPv6 multicast solicitation address from a target address.
     *
     * Constructs address ff02::1:ffxx:xxxx using the last 24 bits of the target.
     * Used for NS and DAD probes.
     *
     * @param targetIp Target IPv6 address.
     * @return Multicast solicitation address.
     *
     * @see RFC 4861 section 5.1.2
     */
    types::IPv6Address generateMulticastSolicitationAddress(types::IPv6Address targetIp);

    /**
     * @brief Constructs a Neighbor Solicitation ICMPv6 packet.
     *
     * Builds the NS message including source link-layer address option if @p currentMac is provided.
     *
     * @param packet PacketBuilder to write into.
     * @param targetIp Target address for the NS.
     * @param currentMac Optional source MAC address (nullptr to omit option).
     */
    void neighborSolicitation(processing::PacketBuilder& packet, types::IPv6Address targetIp, uint64_t* currentMac);

    /**
     * @brief Constructs a Neighbor Advertisement ICMPv6 packet.
     *
     * Builds the NA message with S and O flags set as appropriate.
     *
     * @param packet PacketBuilder to write into.
     * @param currentMac Source MAC address for the NA.
     * @param targetIp Destination address for the NA (nullptr for unspecified).
     */
    void neighborAdvertisement(processing::PacketBuilder& packet, types::Mac currentMac, types::IPv6Address* targetIp);

    /**
     * @brief Constructs a Router Solicitation ICMPv6 packet.
     *
     * Builds the RS message with source link-layer address option.
     *
     * @param packet PacketBuilder to write into.
     * @param currentMac Source MAC address for the RS.
     */
    void routeSolicitation(processing::PacketBuilder& packet, types::Mac currentMac);

    /**
     * @brief Constructs a Router Advertisement ICMPv6 packet.
     *
     * Builds the RA message with configured prefixes, default route lifetime,
     * and IPv6 flags (M, O, etc.).
     *
     * @param packet PacketBuilder to write into.
     * @param currentMac Source MAC address for the RA.
     */
    void routeAdvertisement(processing::PacketBuilder& packet, types::Mac currentMac);

    core::Global& global;                                     ///< Reference to global system controller.
};

} // namespace infrastructure

#endif // NDP_H
