// TcpCongestionControl.cpp
#include "TcpCongestionControl.h"

#include <algorithm>

namespace TCP
{
void TcpCongestionControl::reset() noexcept
{
    cwnd = 0;
    ssthresh = 0;
    inFlight = 0;
}

void TcpCongestionControl::setInitialCwnd(uint32_t cwndBytes) noexcept
{
    cwnd = cwndBytes;
    if (ssthresh == 0) ssthresh = std::max<uint32_t>(cwnd, 65535u);
}

void TcpCongestionControl::onPacketSent(size_t bytes) noexcept
{
    inFlight += static_cast<uint32_t>(bytes);
}

void TcpCongestionControl::onAcked(size_t newlyAckedBytes) noexcept
{
    const uint32_t acked = static_cast<uint32_t>(newlyAckedBytes);
    if (acked > inFlight) inFlight = 0;
    else inFlight -= acked;

    if (cwnd == 0)
    {
        cwnd = std::max<uint32_t>(acked, 1460u);
        if (ssthresh == 0) ssthresh = std::max<uint32_t>(cwnd, 65535u);
        return;
    }

    if (cwnd < ssthresh)
    {
        cwnd += acked;
    }
    else
    {
        // Congestion avoidance (very simple Reno-style):
        // // cwnd += max(1, (mss * acked) / cwnd). MSS approximation kept constant here.
        constexpr std::uint32_t MSS_APPROX = 1460u;
        const std::uint32_t inc = std::max<std::uint32_t>(1u, (MSS_APPROX * std::max(acked, 1u)) / std::max(cwnd, 1u));
        cwnd += inc;
    }
}

void TcpCongestionControl::onFastLoss() noexcept
{
    // Multiplicative decrease.
    ssthresh = std::max<uint32_t>(cwnd / 2, 2u * 1460u);
    cwnd = ssthresh + 3u * 1460u; // fast recovery style bump
}

void TcpCongestionControl::onRtoTimeout() noexcept
{
    ssthresh = std::max<uint32_t>(cwnd / 2, 2u * 1460u);
    cwnd = 1u * 1460u;
    inFlight = 0;
}

uint32_t TcpCongestionControl::cwndBytes() const noexcept { return cwnd; }
uint32_t TcpCongestionControl::ssthreshBytes() const noexcept { return ssthresh; }
uint32_t TcpCongestionControl::bytesInFlight() const noexcept { return inFlight; }

uint32_t TcpCongestionControl::sendWindowBytes(uint32_t peerWndBytes) const noexcept
{
    if (cwnd == 0) return peerWndBytes;
    return std::min(peerWndBytes, cwnd);
}
} // namespace TCP
