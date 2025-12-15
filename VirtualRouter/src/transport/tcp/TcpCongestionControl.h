// TcpCongestionControl.h

#ifndef TCP_CONGESTION_CONTROL_H
#define TCP_CONGESTION_CONTROL_H

#include <cstdint>
#include <cstddef>

namespace TCP
{
class TcpCongestionControl
{
public:
    void reset() noexcept;

    void onPacketSent(size_t bytes) noexcept;
    void onAcked(size_t newlyAckedBytes) noexcept;

    void onFastLoss() noexcept;
    void onRtoTimeout() noexcept;

    uint32_t cwndBytes() const noexcept;
    uint32_t ssthreshBytes() const noexcept;
    uint32_t bytesInFlight() const noexcept;

    uint32_t sendWindowBytes(uint32_t peerWndBytes) const noexcept;
    void setInitialCwnd(uint32_t cwndBytes) noexcept;

private:
    uint32_t cwnd{0};
    uint32_t ssthresh{0};
    uint32_t inFlight{0};
};
}

#endif // TCP_CONGESTION_CONTROL_H
