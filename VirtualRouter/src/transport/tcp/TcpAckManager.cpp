// TcpAckManager.cpp

#include "TcpAckManager.h"
#include <TimeManager.h>

namespace TCP
{
TcpAckManager::TcpAckManager(TimeManager& tmgr)
    : tmgr(tmgr) {}

void TcpAckManager::reset() noexcept
{
    pending = false;
    immediate = false;
    lastAckSent = 0;
    cancelDelayedAck();
}

void TcpAckManager::onSegmentReceived(const TcpSegment& seg, bool inOrder) noexcept
{
    // Minimal policy:
    // - Immediate ACK for out-of-order, SYN, FIN, RST.
    // - Otherwise delayed ACK for in-order data.
    if (!inOrder || seg.hdr.getFlagSYN() || seg.hdr.getFlagFIN() || seg.hdr.getFlagRST()) {
        requestImmediate();
        return;
    }

    if (!seg.payload.empty()) {
        pending = true;
        // Deadline is armed by the connection using its configured delay.
    }
}

void TcpAckManager::requestImmediate() noexcept
{
    pending = true;
    immediate = true;
    cancelDelayedAck();
}

void TcpAckManager::onAckSent(uint32_t ackNo) noexcept
{
    lastAckSent = ackNo;
    pending = false;
    immediate = false;
    cancelDelayedAck();
}

bool TcpAckManager::ackPending() const noexcept { return pending; }
bool TcpAckManager::immediateAckPending() const noexcept { return immediate; }

bool TcpAckManager::isDelayedAck() const noexcept
{
    return delayedId != 0;
}

void TcpAckManager::startDelayedAck(std::chrono::steady_clock::time_point deadline) noexcept
{
    delayedId = tmgr.addTimer(deadline, [id = &delayedId]() {
        *id = 0;
    });
}

void TcpAckManager::cancelDelayedAck() noexcept
{
    if (delayedId != 0)
    {
        tmgr.cancelTimer(delayedId);
        delayedId = 0;
    }
}
}
