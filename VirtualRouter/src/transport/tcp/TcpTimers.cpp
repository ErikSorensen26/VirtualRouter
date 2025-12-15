// TcpTimers.cpp

#include "TcpTimers.h"

namespace TCP
{
void TcpTimers::reset() noexcept
{
    rtoId = 0;
    delayedAckId = 0;
    timeWaitId = 0;
    rto.reset();
    delayedAck.reset();
    timeWait.reset();
}

void TcpTimers::startRto(std::chrono::steady_clock::time_point deadline) noexcept
{
}

void TcpTimers::cancelRto() noexcept
{
}

void TcpTimers::onRtoExpired() const noexcept
{
    if (const auto* e = c.rtxq_.oldest()) {
        auto r = c.rtxq_.buildRetransmission(*e, c.peerMssOrDefault());
        if (r.has_value()) {
            r->src = c.key_.local;
            r->dst = c.key_.remote;
            r->ack = c.rcv_.rcvNxt;
            r->flags |= TcpFlag::Ack;
            r->window = c.flow_.windowFieldFromFreeSpace(c.recvBuf_.freeSpace());
            out.send(*r);
            c.cc_.onRtoTimeout();
            c.timers_.armRto(now + c.cfg_.initialRto);
        }
    } else {
        c.timers_.disarmRto();
    }
}

void TcpTimers::startDelayedAck(std::chrono::steady_clock::time_point deadline) noexcept
{
}

void TcpTimers::cancelDelayedAck() noexcept
{
}

void TcpTimers::onDelayedAckExpired() const noexcept
{
    c.sendAckOnly(out);
}

void TcpTimers::startTimeWait(std::chrono::steady_clock::time_point deadline) noexcept
{
}

void TcpTimers::cancelTimeWait() noexcept
{
}

void TcpTimers::onTimeWaitExpired() const noexcept
{
    if (c.state_ == TcpState::TimeWait && c.timers_.timeWaitExpired(now)) {
        c.state_ = TcpState::Closed;
        c.timers_.disarmTimeWait();
    }
}

std::optional<std::chrono::steady_clock::time_point> TcpTimers::nextDeadline() noexcept
{
    std::optional<std::chrono::steady_clock::time_point> out;
    auto minAssign = [&](const std::optional<std::chrono::steady_clock::time_point>& t)
    {
        if (!t.has_value()) return;
        if (!out.has_value() || *t < *out) out = t;
    };
    minAssign(rto);
    minAssign(delayedAck);
    minAssign(timeWait);
    return out;
}
}
