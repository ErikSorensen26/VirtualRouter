/**
 * @file Fsm.h
 * @brief RFC 4271 BGP finite-state machine.
 */

#ifndef BGP_FSM_H
#define BGP_FSM_H

#include "bgp/BgpTypes.hpp"

namespace routing::bgp
{
/// Callback invoked whenever the FSM completes a state transition.
using FsmTransitionCallback = std::function<void(FsmState from, FsmState to, FsmEvent trigger)>;
class Session;

/**
 * @brief RFC 4271 §8 BGP finite-state machine for one peer session.
 * @ingroup BGP_SESSION
 *
 * `Fsm` implements the six-state BGP FSM exactly as specified in RFC 4271
 * §8.2.  Each state has a dedicated handler (`handleIdle`, `handleConnect`,
 * …) that processes the incoming @ref FsmEvent and decides whether to
 * transition, send a message, or reset.
 *
 * ## Architectural Role
 * `Fsm` is owned by @ref Session and operates on it exclusively.  All
 * side-effects (sending messages, starting timers, opening TCP connections)
 * go through the `Session` reference.  The FSM itself holds no timers or
 * TCP handles — those live in @ref SessionTimers and the `Session` TCP fields.
 *
 * ## Lifecycle & Ownership
 * Constructed by `Session` with a back-reference to itself.  The FSM starts
 * in IDLE and only advances when `processEvent` is called.  There is no
 * explicit teardown; destruction of the owning `Session` is sufficient.
 *
 * ## Concurrency Model
 * `processEvent()` must be called from the `BgpProcess` scheduler thread.
 * The transition callback (@ref setTransitionCallback) fires synchronously
 * within `processEvent()`, also on the scheduler thread.
 *
 * @warning Calling `processEvent()` from outside the scheduler thread is a
 * data race and causes undefined behaviour.
 *
 * @see Session
 * @see FsmEvent
 * @see FsmState
 */
class Fsm
{
public:
    /**
     * @brief Constructs the FSM in the IDLE state.
     *
     * @param session Reference to the owning session; must outlive this object.
     */
    explicit Fsm(Session& session) noexcept;

    Fsm(const Fsm&) = delete;
    Fsm& operator=(const Fsm&) = delete;

    /**
     * @brief Registers a callback that fires on every state transition.
     *
     * The callback receives the previous state, the new state, and the
     * triggering event.  It is invoked synchronously from within
     * `processEvent()`.
     *
     * @param cb Callback to invoke; replaces any previously registered callback.
     */
    void setTransitionCallback(FsmTransitionCallback cb) noexcept
    {
        transitionCallback = std::move(cb);
    }

    /**
     * @brief Feeds an event into the FSM and executes the resulting actions.
     *
     * Dispatches to the per-state handler for the current state, which may
     * change state, send a BGP message, start or stop timers, or initiate a
     * TCP connection.
     *
     * @param event The FSM event to process.
     */
    void processEvent(FsmEvent event);

    FsmState getState() const noexcept { return state; }

private:
    /**
     * @brief Handles events in the IDLE state.
     *
     * MANUAL_START / AUTOMATIC_START transitions to CONNECT and initiates TCP.
     * All other events are silently ignored per RFC 4271 §8.2.2.
     *
     * @param event Event to process.
     */
    void handleIdle(FsmEvent event);

    /**
     * @brief Handles events in the CONNECT state.
     *
     * On TCP_CONNECTION_CONFIRMED sends OPEN and moves to OPEN_SENT.  On
     * TCP_CONNECTION_FAILS or ConnectRetry expiry, resets and retries.
     *
     * @param event Event to process.
     */
    void handleConnect(FsmEvent event);

    /**
     * @brief Handles events in the ACTIVE state.
     *
     * Waits for an inbound TCP connection or ConnectRetry to expire.  On
     * TCP_CONNECTION_CONFIRMED sends OPEN and moves to OPEN_SENT.
     *
     * @param event Event to process.
     */
    void handleActive(FsmEvent event);

    /**
     * @brief Handles events in the OPEN_SENT state.
     *
     * On BGP_OPEN validates the peer OPEN, performs collision detection, and
     * moves to OPEN_CONFIRMED.  Errors send a NOTIFICATION and reset to IDLE.
     *
     * @param event Event to process.
     */
    void handleOpenSent(FsmEvent event);

    /**
     * @brief Handles events in the OPEN_CONFIRM state.
     *
     * On KEEPALIVE_MSG negotiates capabilities and transitions to ESTABLISHED.
     * Hold timer expiry sends NOTIFICATION and resets to IDLE.
     *
     * @param event Event to process.
     */
    void handleOpenConfirm(FsmEvent event);

    /**
     * @brief Handles events in the ESTABLISHED state.
     *
     * Processes UPDATE, KEEPALIVE, ROUTE-REFRESH, and NOTIFICATION messages.
     * Any error or MANUAL_STOP tears down the session.
     *
     * @param event Event to process.
     */
    void handleEstablished(FsmEvent event);

    /**
     * @brief Executes a state transition and fires the registered callback.
     *
     * @param newState Target state.
     * @param trigger  Event that caused the transition.
     */
    void transitionTo(FsmState newState, FsmEvent trigger);

    /**
     * @brief Closes TCP connections, stops all timers, and transitions to IDLE.
     *
     * @param sendNotification If true, a NOTIFICATION message is sent before
     *                         closing the TCP connection.
     * @param notifCode        NOTIFICATION error/subcode to send.
     * @param trigger          Event that caused the reset; reported to transition
     *                         observers so they see the real cause rather than a
     *                         synthetic stop.
     */
    void resetToIdle(bool sendNotification,
                     uint16_t notifCode = BGP_NOTIFICATION_CEASE_UNSPECIFIC,
                     FsmEvent trigger = FsmEvent::MANUAL_STOP);

    /**
     * @brief Resets TCP, restarts the ConnectRetry timer, and initiates a new
     *        outgoing connection without transitioning to IDLE first.
     *
     * Used in CONNECT and ACTIVE when an attempt fails but the session should
     * retry immediately rather than waiting in IDLE.
     */
    void resetAndReconnect();

    /**
     * @brief Initiates an outgoing (active) TCP connection to the peer.
     *
     * Delegates to `session.initiateConnection()`.  Called from IDLE on start
     * events and from `resetAndReconnect`.
     */
    void initiateOutgoingTcp();

    /**
     * @brief Negotiates the hold time and keepalive interval after an OPEN is received.
     *
     * Takes the smaller of the peer's advertised hold time (already stored in
     * `session.holdTime`) and the configured one (RFC 4271 4.2), derives the
     * keepalive interval, and arms both timers. A negotiated hold time of 0
     * disables them.
     *
     * @return False if the peer's hold time is below the configured minimum, in
     *         which case the session has already been reset to Idle with an
     *         Unacceptable Hold Time notification.
     */
    bool negotiateHoldTime();

    FsmState state{FsmState::IDLE};     ///< Current FSM state.
    bool passiveMode{false};            ///< When true, outgoing connections are never initiated.

    FsmTransitionCallback transitionCallback; ///< Optional callback fired on every transition.

    Session& session; ///< Owning session; provides message sending, timer, and TCP access.
};
} // namespace routing

#endif // BGP_FSM_H
