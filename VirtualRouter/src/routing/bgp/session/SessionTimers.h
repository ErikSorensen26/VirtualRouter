/**
 * @file SessionTimers.h
 * @brief BGP session timers: ConnectRetry, Hold, KeepAlive, and IdleHold.
 */

#ifndef BGP_SESSION_TIMERS_H
#define BGP_SESSION_TIMERS_H

#include <atomic>
#include <chrono>
#include <cstdint>

#include <ControlScheduler.h>

namespace routing::bgp
{
class Session;

/**
 * @brief Manages the four RFC 4271 BGP session timers for one neighbor session.
 * @ingroup BGP_SESSION
 *
 * `SessionTimers` wraps the `ControlScheduler` one-shot timer API to provide
 * named BGP timers with restart semantics.  Each timer is identified by an
 * atomic ID so that stale callbacks from a cancelled timer can be detected
 * and silently dropped.
 *
 * The four timers correspond directly to the RFC 4271 §10 timer attributes:
 * - **ConnectRetry** — bounds the time allowed for a TCP connection attempt.
 * - **Hold** — bounds the maximum interval between received KEEPALIVE or UPDATE
 *   messages; expiry tears down the session.
 * - **Keepalive** — triggers outbound KEEPALIVE transmission to keep the hold
 *   timer from expiring on the peer.
 * - **IdleHold** — optional dampening timer that delays automatic reconnection
 *   after a session reset to avoid rapid flapping (RFC 4271 §8 optional attr.).
 *
 * ## Architectural Role
 * `SessionTimers` is owned by @ref Session and is the only object that posts
 * timer-expiry FSM events.  The timers themselves run on the scheduler and
 * post back to the `BgpProcess` queue, so all timer callbacks are serialised
 * with other FSM events.
 *
 * ## Lifecycle & Ownership
 * Constructed by `Session` with a back-reference.  `cancelAll()` must be
 * called before the owning `Session` is destroyed to prevent dangling
 * callbacks.
 *
 * ## Concurrency Model
 * Timer IDs are stored as `std::atomic<uint32_t>` so that cancellation from
 * the scheduler thread and reads from the BGP thread are race-free.  All other
 * members are accessed exclusively from the BGP scheduler thread.
 *
 * @see Session
 * @see Fsm
 */
class SessionTimers
{
public:
    /**
     * @brief Constructs the timer set; no timers are started at construction.
     *
     * @param session Owning session; must outlive this object.
     */
    explicit SessionTimers(Session& session) noexcept;

    SessionTimers(const SessionTimers&) = delete;
    SessionTimers& operator=(const SessionTimers&) = delete;

    // CONNECT RETRY TIMER

    /**
     * @brief Starts the ConnectRetry timer with the given interval.
     *
     * If a ConnectRetry timer is already running it is cancelled first.
     * On expiry, posts `FsmEvent::CONNECTION_RETRY_TIMER_EXPIRES` to the session.
     *
     * @param interval Duration before the timer fires.
     */
    void startConnectRetry(std::chrono::seconds interval);

    /**
     * @brief Cancels the current ConnectRetry timer and starts a new one.
     *
     * Convenience wrapper that calls `stopConnectRetry` then `startConnectRetry`.
     *
     * @param interval New interval for the restarted timer.
     */
    void restartConnectRetry(std::chrono::seconds interval);

    /**
     * @brief Cancels the ConnectRetry timer without firing it.
     */
    void stopConnectRetry() noexcept;

    // HOLD TIMER

    /**
     * @brief Starts the Hold timer with the given hold-time value.
     *
     * The hold time is negotiated during OPEN exchange.  On expiry, posts
     * `FsmEvent::HOLD_TIMER_EXPIRES`, which causes the FSM to send a
     * NOTIFICATION and tear down the session.
     *
     * @param holdTime Negotiated hold time in seconds.
     */
    void startHoldTimer(std::chrono::seconds holdTime);

    /**
     * @brief Cancels the current Hold timer and restarts it at the last configured hold time.
     *
     * Called on receipt of any KEEPALIVE or UPDATE message to reset the
     * expiry window.
     */
    void restartHoldTimer() noexcept;

    /**
     * @brief Cancels the Hold timer without firing it.
     */
    void stopHoldTimer() noexcept;

    // KEEPALIVE TIMER

    /**
     * @brief Starts the periodic KeepAlive timer.
     *
     * On expiry, posts `FsmEvent::KEEPALIVE_TIMER_EXPIRES` and the timer
     * automatically restarts at the same interval.
     *
     * @param interval Interval between outbound KEEPALIVE messages.
     */
    void startKeepaliveTimer(std::chrono::seconds interval);

    /**
     * @brief Cancels the current KeepAlive timer and restarts it at the last interval.
     */
    void restartKeepaliveTimer() noexcept;

    /**
     * @brief Cancels the KeepAlive timer without firing it.
     */
    void stopKeepaliveTimer() noexcept;

    // IDLE HOLD TIMER

    /**
     * @brief Starts the IdleHold dampening timer.
     *
     * On expiry, posts `FsmEvent::IDLE_HOLD_TIMER_EXPIRES`, allowing the FSM
     * to attempt reconnection.  A longer interval is typically used after
     * repeated rapid session resets.
     *
     * @param interval Duration to hold in IDLE before allowing reconnection.
     */
    void startIdleHoldTimer(std::chrono::seconds interval);

    /**
     * @brief Cancels the IdleHold timer without firing it.
     */
    void stopIdleHoldTimer() noexcept;

    /**
     * @brief Cancels all four timers atomically.
     *
     * Must be called before the owning @ref Session is destroyed to prevent
     * stale timer callbacks from firing against a deleted session.
     */
    void cancelAll() noexcept;

    uint32_t connectionRetryCount = 0; ///< Number of consecutive TCP connection attempts since last ESTABLISHED.

private:
    /**
     * @brief Cancels the timer identified by `timerId` and resets the ID to zero.
     *
     * @param timerId Atomic timer ID to cancel; set to 0 on success.
     * @return True if a timer was actually cancelled; false if it had already fired or was not running.
     */
    bool cancel(std::atomic<uint32_t>& timerId) noexcept;

    Session& session;                   ///< Owning session; receives FSM events when timers fire.
    core::ProcessQueue& scheduler;   ///< Scheduler used to post timer-expiry events to the BGP thread.

    std::atomic<uint32_t> connectionRetryTimerId{0}; ///< Scheduler token for the ConnectRetry timer; 0 = not running.
    std::atomic<uint32_t> holdTimerId{0};            ///< Scheduler token for the Hold timer; 0 = not running.
    std::atomic<uint32_t> keepaliveTimerId{0};       ///< Scheduler token for the KeepAlive timer; 0 = not running.
    std::atomic<uint32_t> idleHoldTimerId{0};        ///< Scheduler token for the IdleHold timer; 0 = not running.

    std::chrono::seconds lastHoldTime{0};            ///< Hold time used by the most recent `startHoldTimer` call; needed for `restartHoldTimer`.
    std::chrono::seconds lastKeepaliveInterval{0};   ///< Interval used by the most recent `startKeepaliveTimer` call; needed for `restartKeepaliveTimer`.
};
} // namespace routing::bgp

#endif // BGP_SESSION_TIMERS_H
