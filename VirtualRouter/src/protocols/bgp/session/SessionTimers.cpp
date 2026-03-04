// SessionTimers.cpp

#include "SessionTimers.h"
#include "bgp/session/Session.h"

namespace BGP
{
SessionTimers::SessionTimers(Session& session, ProcessQueueRef queue) noexcept
    : session(session), queue(std::move(queue))
{}

bool SessionTimers::cancel(std::atomic<uint32_t>& timerId) noexcept
{
    uint32_t id = timerId.exchange(0, std::memory_order_acq_rel);
    if (id == 0) return false;
    return queue.cancel(id);
}

void SessionTimers::startConnectRetry(std::chrono::seconds interval)
{
    cancel(connectionRetryTimerId);

    auto expiry = std::chrono::steady_clock::now() + interval;
    uint32_t id = queue.postAfter(expiry, [this](uint32_t) {
        connectionRetryCount++;
        session.postEvent(FsmEvent::CONNECTION_RETRY_TIMER_EXPIRES);
    });
    connectionRetryTimerId.store(id, std::memory_order_release);
}

void SessionTimers::restartHoldTimer() noexcept
{
    if (lastHoldTime.count() == 0) return;
    cancel(holdTimerId);

    auto expiry = std::chrono::steady_clock::now() + lastHoldTime;
    uint32_t id = queue.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::HOLD_TIMER_EXPIRES);
    });
    holdTimerId.store(id, std::memory_order_release);
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
    uint32_t id = queue.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::KEEPALIVE_TIMER_EXPIRES);
    });
    keepaliveTimerId.store(id, std::memory_order_release);
}

void SessionTimers::restartKeepaliveTimer() noexcept
{
    if (lastKeepaliveInterval.count() == 0) return;
    auto expiry = std::chrono::steady_clock::now() + lastKeepaliveInterval;
    uint32_t id = queue.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::KEEPALIVE_TIMER_EXPIRES);
    });
    keepaliveTimerId.store(id, std::memory_order_release);
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
    uint32_t id = queue.postAfter(expiry, [this](uint32_t) {
        session.postEvent(FsmEvent::IDLE_HOLD_TIMER_EXPIRES);
    });
    idleHoldTimerId.store(id, std::memory_order_release);
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
}
