// SessionTimers.cpp

#include "SessionTimers.h"
#include "bgp/session/Session.h"
#include "bgp/neighbor/Neighbor.h"

namespace routing::bgp
{
SessionTimers::SessionTimers(Session& session, core::ProcessQueue& schldr) noexcept
    : session(session), scheduler(schldr)
{}

bool SessionTimers::cancel(uint32_t& timerId) noexcept
{
    if (timerId == 0) return false;
    bool canceled = scheduler.cancel(timerId);
    timerId = 0;
    return canceled;
}

void SessionTimers::startConnectRetry(std::chrono::seconds interval)
{
    cancel(connectionRetryTimerId);

    auto expiry = std::chrono::steady_clock::now() + interval;
    connectionRetryTimerId = scheduler.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::CONNECTION_RETRY_TIMER_EXPIRES);
    });
}

void SessionTimers::restartConnectRetry(std::chrono::seconds interval)
{
    startConnectRetry(interval);
}

void SessionTimers::stopConnectRetry() noexcept
{
    cancel(connectionRetryTimerId);
}

void SessionTimers::startHoldTimer(std::chrono::seconds holdTime)
{
    lastHoldTime = holdTime;
    cancel(holdTimerId);
    if (holdTime.count() == 0) return;

    auto expiry = std::chrono::steady_clock::now() + holdTime;
    holdTimerId = scheduler.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::HOLD_TIMER_EXPIRES);
    });
}

void SessionTimers::restartHoldTimer() noexcept
{
    if (lastHoldTime.count() == 0) return;
    cancel(holdTimerId);

    auto expiry = std::chrono::steady_clock::now() + lastHoldTime;
    holdTimerId = scheduler.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::HOLD_TIMER_EXPIRES);
    });
}

void SessionTimers::stopHoldTimer() noexcept
{
    cancel(holdTimerId);
}

void SessionTimers::startKeepaliveTimer(std::chrono::seconds interval)
{
    lastKeepaliveInterval = interval;
    cancel(keepaliveTimerId);
    if (interval.count() == 0) return;

    auto expiry = std::chrono::steady_clock::now() + interval;
    keepaliveTimerId = scheduler.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::KEEPALIVE_TIMER_EXPIRES);
    });
}

void SessionTimers::restartKeepaliveTimer() noexcept
{
    if (lastKeepaliveInterval.count() == 0) return;
    cancel(keepaliveTimerId);
    auto expiry = std::chrono::steady_clock::now() + lastKeepaliveInterval;
    keepaliveTimerId = scheduler.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::KEEPALIVE_TIMER_EXPIRES);
    });
}

void SessionTimers::stopKeepaliveTimer() noexcept
{
    cancel(keepaliveTimerId);
}

void SessionTimers::startIdleHoldTimer(std::chrono::seconds interval)
{
    cancel(idleHoldTimerId);
    if (interval.count() == 0)
    {
        session.postEvent(FsmEvent::IDLE_HOLD_TIMER_EXPIRES);
        return;
    }

    auto expiry = std::chrono::steady_clock::now() + interval;
    idleHoldTimerId = scheduler.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::IDLE_HOLD_TIMER_EXPIRES);
    });
}

void SessionTimers::stopIdleHoldTimer() noexcept
{
    cancel(idleHoldTimerId);
}

void SessionTimers::cancelAll() noexcept
{
    cancel(connectionRetryTimerId);
    cancel(holdTimerId);
    cancel(keepaliveTimerId);
    cancel(idleHoldTimerId);
}
} // namespace routing
