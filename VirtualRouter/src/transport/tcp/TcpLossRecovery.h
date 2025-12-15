// TcpLossRecovery.h

#ifndef TCP_LOSS_RECOVERY_H
#define TCP_LOSS_RECOVERY_H

#include <TcpSegment.hpp>

namespace TCP
{
class TcpLossRecovery
{
public:
    void reset() noexcept;

    void onAckProcessed(TcpAck ackNo, bool advancedAck) noexcept;
    void onDuplicateAck() noexcept;

    bool shouldFastRetransmit() const noexcept;
    void onFastRetransmitSent() noexcept;

    uint32_t dupAckCount() const noexcept;

private:
    TcpAck lastAck{0};
    uint32_t dupAcks{0};
    bool fastRetxFired{false};
};
}

#endif // TCP_LOSS_RECOVERY_H
