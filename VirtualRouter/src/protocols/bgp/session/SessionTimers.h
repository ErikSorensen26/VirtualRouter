// SessionTimers.h

#ifndef BGP_SESSION_TIMERS_H
#define BGP_SESSION_TIMERS_H

#include <atomic>
#include <chrono>
#include <cstdint>

#include <ControlScheduler.h>

namespace BGP
{
class Session;

/**
 * @class SessionTimers
 * @brief Manages the four RFC 4271 BGP session timers for one neighbor session.
 *
 * All timer callbacks are posted to the Session's ProcessQueue so that
 * they are serialised with respect to other FSM events.
 *
 * Timers:
 *   ConnectRetry  – fires when a TCP connection attempt times out
 *   HoldTimer     – fires when no KEEPALIVE / UPDATE is received in time
 *   Keepalive     – fires to trigger outbound KEEPALIVE transmission
 *   IdleHold      – dampens session flapping (RFC 4271 §8 optional attr.)
 */
class SessionTimers
{
public:
    explicit SessionTimers(Session& session, ProcessQueueRef queue) noexcept;

    SessionTimers(const SessionTimers&) = delete;
    SessionTimers& operator=(const SessionTimers&) = delete;

    // ConnectRetry timer
    void startConnectRetry(std::chrono::seconds interval);
    void restartConnectRetry(std::chrono::seconds interval);
    void stopConnectRetry() noexcept;

    // Hold timer
    void startHoldTimer(std::chrono::seconds holdTime);
    void restartHoldTimer() noexcept;
    void stopHoldTimer() noexcept;

    // Keepalive Timer
    void startKeepaliveTimer(std::chrono::seconds interval);
    void restartKeepaliveTimer() noexcept;
    void stopKeepaliveTimer() noexcept;

    // Idle hold timer
    void startIdleHoldTimer(std::chrono::seconds interval);
    void stopIdleHoldTimer() noexcept;

    void cancelAll() noexcept;

    uint32_t connectionRetryCount = 0;

private:
    bool cancel(std::atomic<uint32_t>& timerId) noexcept;

    Session& session;
    ProcessQueueRef queue;

    std::atomic<uint32_t> connectionRetryTimerId{0};
    std::atomic<uint32_t> holdTimerId{0};
    std::atomic<uint32_t> keepaliveTimerId{0};
    std::atomic<uint32_t> idleHoldTimerId{0};

    std::chrono::seconds lastHoldTime{0};
    std::chrono::seconds lastKeepaliveInterval{0};
};
}

#endif // BGP_SESSION_TIMERS_H
