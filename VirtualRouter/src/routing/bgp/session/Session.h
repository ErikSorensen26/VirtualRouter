/**
 * @file Session.h
 * @brief BGP session state, message exchange, and TCP connection management for one peer.
 */

/**
 * @defgroup BGP_SESSION BGP Session
 * @ingroup BGP
 * @brief FSM, session timers, collision detection, capabilities, and multi-session support.
 */

#ifndef BGP_SESSION_H
#define BGP_SESSION_H

#include <span>
#include <cstdint>
#include <optional>

#include "tcp/Connection.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/session/Fsm.h"
#include "bgp/session/SessionTimers.h"
#include "configs/registry/router/BgpRegistry.h"
#include "Capabilities.hpp"
#include "MultiSession.h"

namespace processing { class PacketBuilder; }

namespace routing::bgp
{
class Neighbor;
class BgpProcess;
class MultiSession;

/**
 * @brief Manages the full lifecycle of one BGP session with a single peer.
 * @ingroup BGP_SESSION
 *
 * A `Session` is the runtime object for one configured BGP neighbor.  It
 * drives the RFC 4271 finite-state machine, owns the timer set, holds the
 * TCP connection(s), and serialises all outbound messages.
 *
 * Each `Session` owns:
 * - An @ref Fsm instance that implements the six RFC 4271 states.
 * - A @ref SessionTimers set (ConnectRetry, Hold, KeepAlive, IdleHold).
 * - Up to two @ref transport::tcp::Connection objects: one active (outbound)
 *   and one passive (inbound).  `primaryConn` points to whichever is live.
 * - Local and peer @ref Capabilities structs, plus the @ref NegotiatedCapabilities
 *   derived after OPEN exchange.
 * - A `std::variant<AfiSafi, MultiSession>` that tracks either the single
 *   negotiated AFI/SAFI or the full multi-session state when the peer
 *   supports RFC 8277-style multi-session.
 *
 * ## Architectural Role
 * `Session` is the boundary between the transport layer (TCP) and the BGP
 * protocol layer (FSM, RIB, policy).  It does not touch the RIB directly —
 * all RIB operations are dispatched through `BgpProcess` after `handleIncoming`
 * demultiplexes the wire bytes into typed events.
 *
 * ## Lifecycle & Ownership
 * Sessions are created by `BgpProcess` in response to `onAcceptCallback`
 * (passive) or operator configuration (active) and stored in the process's
 * session map keyed by `ConnId`.  Construction establishes the FSM in IDLE
 * and populates local capabilities.  The destructor closes any open TCP
 * connections; callers must ensure the FSM is not mid-transition.
 *
 * ## Concurrency Model
 * All public methods except the static TCP callbacks must be called from the
 * `BgpProcess` scheduler thread.  The static callbacks (`onConnectCallback`,
 * `onReceiveCallback`) are invoked by the TCP subsystem on its own thread and
 * post events back to the scheduler queue before touching any session state.
 *
 * @warning Calling any non-static method from outside the scheduler thread is
 * a data race and causes undefined behaviour.
 *
 * @see Fsm
 * @see SessionTimers
 * @see BgpProcess
 * @see Neighbor
 */
class Session
{
public:
    /**
     * @brief Constructs a session for a neighbor that will negotiate a single AFI/SAFI.
     *
     * Initialises the FSM in IDLE, builds local capabilities from the neighbor's
     * configuration, and leaves TCP connections unopened.
     *
     * @param nbr Neighbor configuration object; must outlive this session.
     */
    Session(Neighbor& nbr) noexcept;

    /**
     * @brief Constructs a multi-session instance bound to a specific AFI/SAFI.
     *
     * Used when a parent @ref MultiSession spawns a child session for one
     * address family.  Capability advertisement is scoped to the given family.
     *
     * @param nbr Neighbor configuration object; must outlive this session.
     * @param afi The specific address family this session is responsible for.
     */
    Session(Neighbor& nbr, const AfiSafi& afi) noexcept;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept = delete;
    Session& operator=(Session&&) noexcept = delete;

    /**
     * @brief Destructs the session, closing any open TCP connections.
     *
     * Does not send a NOTIFICATION before closing; callers that want a
     * graceful teardown should call `sendNotification` + `closeAllConnections`
     * before destroying the object.
     */
    ~Session();

    // TCP MANAGEMENT

    /**
     * @brief Accepts an inbound TCP connection and posts TCP_CONNECTION_CONFIRMED to the FSM.
     *
     * Stores the connection as `passiveConn` and sets `primaryConn` if no
     * active connection is already live.  Collision detection is the caller's
     * responsibility before invoking this method.
     *
     * @param conn Newly accepted TCP connection; ownership is transferred.
     */
    void acceptConnection(transport::tcp::Connection&& conn);

    /**
     * @brief Initiates an outbound TCP connection to the configured peer address.
     *
     * Registers `onConnectCallback` and `onReceiveCallback` with the TCP
     * engine.  Posts TCP_CR_ACKED or TCP_CONNECTION_FAILS to the FSM once the
     * attempt completes.
     */
    void initiateConnection();

    /**
     * @brief Closes and discards the active (outbound) TCP connection.
     */
    void closeActiveConnection() noexcept;

    /**
     * @brief Closes and discards the passive (inbound) TCP connection.
     */
    void closePassiveConnection() noexcept;

    /**
     * @brief Closes both active and passive connections in one call.
     */
    void closeAllConnections() noexcept;

    // MULTI-SESSION

    /**
     * @brief Returns a pointer to the MultiSession if this session is in multi-session mode.
     *
     * @return Pointer to the @ref MultiSession variant, or nullptr if operating
     *         in single-session mode.
     */
    MultiSession* getMultiSession();

    /**
     * @brief Returns the AFI/SAFI stored in the single-session variant, or nullptr.
     *
     * @return Pointer to the negotiated @ref AfiSafi, or nullptr if the variant
     *         holds a @ref MultiSession instead.
     */
    AfiSafi* getMultiSessionAfi();

    /**
     * @brief Transitions into multi-session mode and initiates an active sub-session for the given family.
     *
     * Converts the variant to a @ref MultiSession and calls `initiateConnection`
     * for the specified AFI/SAFI child session.
     *
     * @param family Address family to establish the active sub-session for.
     */
    void startActiveMultiSession(const AfiSafi& family);

    /**
     * @brief Transitions into multi-session mode and prepares to accept a passive sub-session.
     *
     * @param family Address family expected on the incoming sub-session.
     */
    void startPassiveMultiSession(const AfiSafi& family);

    /**
     * @brief Reads and processes all available BGP messages from the receive buffer.
     *
     * Consumes bytes from `consumer`, frames complete BGP messages, and
     * dispatches to `onOpenReceived`, `onKeepaliveReceived`, `onUpdateReceived`,
     * `onRouteRefreshReceived`, or `onNotificationReceived` based on message
     * type.  Malformed frames trigger a NOTIFICATION and FSM reset.
     *
     * @param consumer TCP receive-buffer accessor for the current connection.
     */
    void handleIncoming(transport::tcp::RxConsumer& consumer);

    /**
     * @brief Posts an FSM event to the scheduler queue for serialised processing.
     *
     * Safe to call from any thread; the event is enqueued and processed by
     * the scheduler on the BgpProcess thread.
     *
     * @param event FSM event to enqueue.
     */
    void postEvent(FsmEvent event);

    /**
     * @brief Called by the FSM after every state transition to execute side-effects.
     *
     * Handles transitions that require action at the Session level, such as
     * notifying `BgpProcess` of ESTABLISHED / session-down events, sending the
     * initial KEEPALIVE on entering OPEN_CONFIRMED, and resetting capabilities
     * when dropping back to IDLE.
     *
     * @param from  State before the transition.
     * @param to    State after the transition.
     * @param trigger Event that caused the transition.
     */
    void onFsmTransition(FsmState from, FsmState to, FsmEvent trigger);

    // OUTBOUND MESSAGE BUILDERS

    /**
     * @brief Builds and sends a BGP OPEN message to the peer.
     *
     * Encodes local AS, hold time, Router ID, and all local capabilities into
     * wire format and writes to `primaryConn`.
     */
    void sendOpen();

    /**
     * @brief Builds and sends a BGP KEEPALIVE message to the peer.
     */
    void sendKeepalive();

    /**
     * @brief Builds and sends a BGP NOTIFICATION message, then closes TCP.
     *
     * The NOTIFICATION is sent synchronously before `closeAllConnections` is
     * called.  The FSM is expected to transition to IDLE after this call.
     *
     * @param notif Notification containing the error/subcode and optional data.
     */
    void sendNotification(const Notification& notif);

    /**
     * @brief Convenience overload that sends a NOTIFICATION with no data payload.
     *
     * @param code Packed error code: high byte = error code, low byte = subcode.
     */
    void sendNotification(uint16_t code);

    /**
     * @brief Sends a ROUTE-REFRESH request to the peer for the given AFI/SAFI.
     *
     * @param family  Address family to request a refresh for.
     * @param reason  Enhanced Route Refresh subtype (Normal, BORR, or EORR).
     */
    void sendRouteRefresh(const AfiSafi& family, RouteRefreshReason reason = RouteRefreshReason::Normal);

    /**
     * @brief Encodes and sends a BGP UPDATE message.
     *
     * @tparam N  NLRI policy type providing `N::Nlri` (the prefix encoding type),
     *            `N::afi` and `N::safi` constants, and a compatible `BuildUpdate`
     *            specialisation.
     * @param update  Pre-built update structure containing attributes and NLRI.
     */
    template <typename N>
    void sendUpdate(const BuildUpdate<typename N::Nlri>& update);

    // INBOUND MESSAGE HANDLERS

    /**
     * @brief Processes a received BGP OPEN message.
     *
     * Validates version, AS number, hold time, and capabilities.  Posts
     * BGP_OPEN or BGP_OPEN_MSG_ERR to the FSM accordingly.
     */
    void onOpenReceived();

    /**
     * @brief Processes a received BGP KEEPALIVE message and restarts the hold timer.
     */
    void onKeepaliveReceived();

    /**
     * @brief Processes a received BGP UPDATE message and hands it to the AddressFamilyInstance.
     */
    void onUpdateReceived();

    /**
     * @brief Processes a received BGP ROUTE-REFRESH message.
     */
    void onRouteRefreshReceived();

    /**
     * @brief Processes a received BGP NOTIFICATION message and tears down the session.
     *
     * Decodes the error code and subcode from `data`, logs the reason, and
     * posts NOTIF_MSG or NOTIF_MSG_VER_ERR to the FSM.
     *
     * @param data Raw NOTIFICATION message bytes starting after the fixed header.
     */
    void onNotificationReceived(std::span<const uint8_t> data);

    // CAPABILITY STATE (simple getters — no comment per style guide)

    Capabilities& getLocalCaps() noexcept { return localCaps; }
    const Capabilities& getLocalCaps() const noexcept { return localCaps; }
    Capabilities& getPeerCaps() noexcept { return peerCaps; }
    const Capabilities& getPeerCaps() const noexcept { return peerCaps; }
    NegotiatedCapabilities& getNegotiated() noexcept { return negotiated; }
    const NegotiatedCapabilities& getNegotiated() const noexcept { return negotiated; }

    uint16_t holdTime = 180;            ///< Negotiated hold time in seconds; updated after OPEN exchange.
    uint16_t keepaliveInterval = 60;    ///< Derived keepalive interval (holdTime / 3 or config override).

    // QUERIES

    bool established() const noexcept { return fsm.getState() == FsmState::ESTABLISHED; }
    FsmState getFsmState() const noexcept { return fsm.getState(); }
    uint32_t getPeerRid() const noexcept { return peerRouterId; }

    /**
     * @brief Returns true if this session is to an external BGP peer (different AS).
     */
    bool isEbgp() const noexcept;

    /**
     * @brief Returns true if this session is to a confederation external peer.
     */
    bool isConfedEbgp() const noexcept;

    bool isOutgoing() const noexcept { return activeConn.has_value(); }

    void setPeerRid(uint32_t rid) { peerRouterId = rid; }

    Neighbor& getNeighbor() noexcept { return neighbor; }
    const Neighbor& getNeighbor() const noexcept { return neighbor; }
    SessionTimers& getTimers() noexcept { return timers; }
    const config::BgpBaseRegistry& getBaseConfig() const noexcept { return base; }

    transport::tcp::Connection* getPrimaryConnection() noexcept { return primaryConn; }
    const transport::tcp::Connection* getPrimaryConnection() const noexcept { return primaryConn; }

    /**
     * @brief Confirms that the given connection ID belongs to this session.
     *
     * Used by `BgpProcess` to route incoming receive callbacks to the correct
     * session when multiple connections exist.
     *
     * @param cid TCP connection identifier to check.
     * @return True if `cid` matches the active or passive connection.
     */
    bool verifyConnection(uint64_t cid);

    /**
     * @brief Performs RFC 4271 §6.8 collision detection against an incoming OPEN.
     *
     * Compares the incoming peer Router ID with the local Router ID to
     * determine which session to keep.  The losing session is terminated with
     * OPEN_COLLISION_DUMP.
     *
     * @param incomingPeerRid Router ID extracted from the peer's OPEN message.
     * @return True if this session wins and should continue; false if it loses
     *         and the FSM has been posted OPEN_COLLISION_DUMP.
     */
    bool resolveCollision(uint32_t incomingPeerRid);

    /**
     * @brief Derives @ref NegotiatedCapabilities from local and peer @ref Capabilities.
     *
     * Called after both OPEN messages have been received.  The result is stored
     * in `negotiated` and governs feature availability for the rest of the session.
     */
    void negotiateCapabilities();

    /**
     * @brief TCP connect-result callback invoked by the transport layer.
     *
     * Converts the TCP event into BGP_TCP_CR_ACKED / TCP_CONNECTION_FAILS and
     * posts it to the session's scheduler queue.  `ctx.user` must point to
     * the owning @ref Session.
     *
     * @param ctx Callback context provided by the TCP engine.
     */
    static void onConnectCallback(transport::tcp::ConnCallbackCtx& ctx) noexcept;

    /**
     * @brief TCP receive callback invoked by the transport layer when data arrives.
     *
     * Posts a receive-ready event to the scheduler queue so that
     * `handleIncoming` is called from the BGP thread.  `ctx.user` must point
     * to the owning @ref Session.
     *
     * @param ctx Callback context provided by the TCP engine.
     */
    static void onReceiveCallback(transport::tcp::RecvCallbackCtx& ctx) noexcept;

private:
    /**
     * @brief Populates `localCaps` from the neighbor and process configuration.
     *
     * Called once at construction.  Reads enabled AFI/SAFIs, 4-byte ASN support,
     * graceful restart parameters, and other optional capabilities.
     */
    void buildLocalCapabilities();

    // REFERENCES
    Neighbor& neighbor;             ///< Owning neighbor; provides configuration and AF-instance access.
    config::BgpBaseRegistry& base;  ///< Base BGP session configuration (timers, AS, router-id, etc.).

    // PROTOCOL STATE
    Fsm fsm;            ///< RFC 4271 finite-state machine; all state transitions go through here.
    SessionTimers timers; ///< ConnectRetry / Hold / KeepAlive / IdleHold timers.

    // TCP CONNECTIONS
    std::optional<transport::tcp::Connection> activeConn;   ///< Outbound (active) TCP connection; absent when not initiating.
    std::optional<transport::tcp::Connection> passiveConn;  ///< Inbound (passive) TCP connection; absent when no peer has connected.
    transport::tcp::Connection* primaryConn = nullptr;      ///< Points to whichever connection is currently the live BGP channel.

    std::variant<AfiSafi, MultiSession> multiSession{std::in_place_type<MultiSession>}; ///< Single-family tag or full multi-session state.

    // CAPABILITY STATE
    Capabilities localCaps;             ///< Capabilities advertised in our OPEN message.
    Capabilities peerCaps;              ///< Capabilities decoded from the peer's OPEN message.
    NegotiatedCapabilities negotiated;  ///< Intersection of local and peer capabilities; governs session behavior.

    uint32_t peerRouterId = 0;          ///< Router ID extracted from the peer's OPEN message; 0 before OPEN is received.
    std::vector<uint8_t> updateSentQueue; ///< Accumulation buffer for UPDATE bytes awaiting transmission.
};
} // namespace routing

#endif // BGP_SESSION_H
