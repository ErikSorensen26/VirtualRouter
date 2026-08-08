/**
 * @file BgpProcess.h
 * @brief Top-level BGP process object — entry point for the entire BGP subsystem.
 */

/**
 * @defgroup BGP BGP
 * @ingroup ROUTING
 * @brief Full RFC 4271 BGP implementation with MP-BGP, AS4, and route reflection.
 */

#ifndef BGP_PROCESS_H
#define BGP_PROCESS_H

#include <cstdint>
#include <ControlScheduler.h>

#include "tcp/Listener.h"
#include "bgp/af/NlriPolicy.hpp"
#include "bgp/neighbor/NeighborTable.h"
#include "bgp/session/Session.h"
#include "bgp/transport/BgpTx.h"
#include "bgp/attributes/AttributeManager.hpp"
#include "bgp/af/AddressFamily.hpp"
#include "bgp/af/AddressFamilyInstance.h" // IWYU pragma: keep

namespace core { class VirtualRouter; }
namespace config { struct BgpBaseRegistry; struct BgpRegistry; }

class Internal_BgpTest;

/**
 * @namespace routing::bgp
 * @brief BGP routing protocol implementation.
 *
 * Contains the full RFC 4271 BGP-4 stack including session management, the
 * RFC 4271 finite-state machine, MP-BGP address-family instances, the RIB
 * decision process, and attribute flyweight management.  All FSM work is
 * serialised through the @ref BgpProcess scheduler (ProcessQueue).
 */
namespace routing::bgp
{
class BgpNeighbor;
class Session;
class BgpRx;
class BgpTx;

/**
 * @brief Top-level BGP process object for one AS number / VRF pair.
 * @ingroup BGP
 *
 * BgpProcess is the root owner of every runtime object in the BGP subsystem.
 * It contains:
 * - A @ref NeighborTable of all configured and dynamic neighbors.
 * - One @ref AddressFamilyInstance per enabled AFI/SAFI, stored in
 *   `addressFamilies` as an `AddressFamilyVariant`.
 * - One @ref Session per established or connecting peer (keyed by peer address).
 * - A shared @ref AttributeManager (flyweight path-attribute store).
 * - A `ProcessQueue` (`scheduler`) that serialises all FSM and RIB work.
 * - A TCP `Listener` that accepts inbound BGP connections on port 179.
 *
 * ## Architectural Role
 * BgpProcess sits between the CLI/config layer (which configures neighbors and
 * address families) and the per-session FSM layer.  It does not implement the
 * RFC 4271 FSM directly; that lives in @ref Fsm, owned by each @ref Session.
 * BgpProcess provides the scheduling context that all FSM callbacks post work
 * into, ensuring that no two sessions mutate shared state concurrently.
 *
 * ## Lifecycle & Ownership
 * Constructed by the routing infrastructure with an AS number and a pointer to
 * the owning VRF (@ref core::VirtualRouter).  The destructor closes the TCP
 * listener and cancels all scheduler timers before freeing sessions and
 * address-family instances.
 *
 * ## Concurrency Model
 * The `scheduler` ProcessQueue is the single serialisation point.  TCP stack
 * callbacks (`onConnectCallback`, `onAcceptCallback`, `onReceiveCallback`) are
 * static and post events into the scheduler — they never touch FSM state
 * directly.  All methods that modify sessions, the neighbor table, or RIBs
 * must run on the scheduler thread.
 *
 * @warning Calling any non-const method from outside the scheduler thread
 * without explicit synchronisation causes a data race and undefined behaviour.
 *
 * @see Session
 * @see AddressFamilyInstance
 * @see NeighborTable
 */
class BgpProcess
{
public:
    /**
     * @brief Constructs a BGP process for the given AS and VRF.
     *
     * Initializes the TCP listener on port 179, creates the scheduler queue,
     * and registers the three static TCP callbacks.  No sessions or address
     * families are started at construction time; those are driven by config
     * sync via @ref NeighborTable::syncNeighbors() and @ref enableAddressFamily().
     *
     * @param as  Local autonomous-system number (1–4294967295).
     * @param vrf Pointer to the owning VRF; must outlive this object.
     */
    BgpProcess(uint32_t as, core::VirtualRouter* vrf);

    /**
     * @brief Destroys the BGP process.
     *
     * Performs an ordered teardown:
     * - Stops the TCP listener (no new inbound connections accepted).
     * - Cancels all pending scheduler timers.
     * - Sends NOTIFICATION (Cease) on every established session.
     * - Destroys all Session objects, then AddressFamilyInstance objects,
     *   then the NeighborTable (order matters: sessions hold references into
     *   the neighbor table).
     */
    ~BgpProcess();

    const uint32_t asNumber; ///< Local AS number — immutable after construction.
    uint32_t getRouterId() const;

    static void onConnectCallback(transport::tcp::ConnCallbackCtx& ctx) noexcept;
    static void onAcceptCallback(transport::tcp::AcceptCallbackCtx& ctx) noexcept;
    static void onReceiveCallback(transport::tcp::RecvCallbackCtx& ctx) noexcept;

    void enqueueSyncNeighbors();
    void enqueueSyncAddressFamilies();
    void enqueueMarkAllAfDirty(AfDirty category);
    void enqueueMarkAllInbound(InDirty category);
    void enqueueMarkAllOutbound(OutAttr attr);
    void enqueueRestartAllSessions();
    void enqueueSyncConfederation();

    core::VirtualRouter& routingInstance; ///< Owning VRF; set at construction.

private:
    friend class NeighborTable;
    friend class Session;
    friend class PeerTemplateTable;
    friend class ProcessAccessor;
    friend class ::Internal_BgpTest;
    friend class BgpRx; // resolves findAddressFamily() while dispatching inbound UPDATEs


    /**
     * @brief Looks up an active session by peer IP address.
     *
     * @return Pointer to the session, or `nullptr` if no session exists for
     *         the given address.
     */
    Session* findSession(const types::IPAddress& addr);

    /**
     * @brief Initiates an outbound (active) TCP connection to the neighbor.
     *
     * Creates a @ref Session if one does not already exist, then starts the
     * FSM with a `MANUAL_START` event.  If the neighbor's SHUTDOWN config flag
     * is set the call is a no-op.
     *
     * @warning Must be called from the scheduler thread.
     */
    void startActiveSession(Neighbor& nbr);

    /**
     * @brief Prepares a neighbor to accept an inbound (passive) connection.
     *
     * Creates a @ref Session configured for passive mode and posts
     * `MANUAL_START_PASSIVE_TCP` to the FSM.
     *
     * @warning Must be called from the scheduler thread.
     */
    void startPassiveSession(Neighbor& nbr);

    /**
     * @brief Administratively shuts down the neighbor session.
     *
     * Sends a NOTIFICATION (Cease / Administrative Shutdown) and transitions
     * the FSM to Idle.  The session object is retained so that it can be
     * restarted via @ref unshutdownNeighbor without reconfiguration.
     */
    void shutdownNeighbor(Neighbor& nbr);

    /**
     * @brief Lifts an administrative shutdown and restarts the session.
     *
     * Clears the shutdown flag and restarts the session via
     * @ref startActiveSession or @ref startPassiveSession depending on whether
     * the neighbor is configured as active or passive.
     */
    void unshutdownNeighbor(Neighbor& nbr);

    /**
     * @brief Called by @ref Session when the FSM transitions to ESTABLISHED.
     *
     * Registers the peer's Router ID in the neighbor table and notifies every
     * enabled @ref AddressFamilyInstance to begin sending initial UPDATEs to
     * the new peer.
     */
    void onSessionEstablished(Session& session);

    /**
     * @brief Called by @ref Session when the FSM leaves the ESTABLISHED state.
     *
     * Withdraws all routes learned from the peer, clears the RID-to-neighbor
     * mapping, and sets `Neighbor::session = nullptr`.
     */
    void onSessionDown(Session& session);
/**
     * @brief Looks up an address-family instance by AFI/SAFI at runtime.
     *
     * @return Pointer to the variant wrapper, or `nullptr` if the AF has not
     *         been enabled.
     */
    AddressFamilyVariant* findAddressFamily(const AfiSafi& afi);

    /**
     * @brief Invokes `fn(afi)` for every enabled address family.
     *
     * @tparam F Callable with signature `void(const AfiSafi&)`.
     */
    template <typename F>
    void forEachAf(F&& fn) const
    {
        for (const auto& [afi, afv] : priv.addressFamilies)
            fn(afi);
    }

    /**
     * @brief Removes and destroys an address-family instance.
     *
     * Withdraws all Loc-RIB routes for the AF from the global RIB before
     * destroying the instance.
     */
    void disableAddressFamily(AfiSafi& af);

    /**
     * @brief Looks up a compile-time-known address family instance.
     *
     * @tparam AF  AFI/SAFI constant.  Must satisfy `hasAddressFamily<AF>()`.
     * @return Pointer to the `AddressFamily<AF>` instance, or `nullptr` if not
     *         yet enabled.
     */
    template <AfiSafi AF>
    const AddressFamily<AF>* findAddressFamily()
    {
        static_assert(hasAddressFamily<AF>(), "types::AddressFamily not supported");
        if (auto it = priv.addressFamilies.find(AF); it != priv.addressFamilies.end())
            return &std::get<AddressFamily<AF>>(it->second);
        return nullptr;
    }

    /**
     * @brief Enables and returns an address-family instance, creating it if needed.
     *
     * Idempotent: if the AF is already enabled the existing instance is
     * returned.  The new instance subscribes to the appropriate network-command
     * RIB watches inside its constructor.
     *
     * @tparam AF  AFI/SAFI constant.  Must satisfy `hasAddressFamily<AF>()`.
     */
    template <AfiSafi AF>
    const AddressFamily<AF>& enableAddressFamily()
    {
        static_assert(hasAddressFamily<AF>(), "types::AddressFamily not supported");
        if (auto it = priv.addressFamilies.find(AF); it != priv.addressFamilies.end())
            return std::get<AddressFamily<AF>>(it->second);
        auto [it, ok] = priv.addressFamilies.try_emplace(AF, std::in_place_type<AddressFamily<AF>>, *this, AF);
        return std::get<AddressFamily<AF>>(it->second);
    }

    core::ProcessQueue scheduler; ///< Single-threaded event queue; all BGP FSM work runs here.
    AttributeManager attrMgr;     ///< Flyweight store for path attributes shared across all sessions.
    NeighborTable ntable;         ///< Configured and dynamic neighbor registry.

    config::BgpRegistry& configs;         ///< Process-level BGP configuration.
private:

    struct Private
    {
        Private(BgpProcess& proc);

        const uint32_t rid;

        /// Schedules the next BGP scan-time timer (BGP_SCAN_TIME config).
        void scheduleScan();

        transport::tcp::Listener listener;                                   ///< Passive TCP listener on port 179.

        std::unordered_map<AfiSafi, AddressFamilyVariant> addressFamilies;   ///< Enabled AFI/SAFI instances.

        std::unordered_map<types::IPAddress, Session> sessions;              ///< Live sessions, keyed by peer IP.

        BgpProcess& process;
    } priv;
};
} // namespace routing

#endif // BGP_PROCESS_H

