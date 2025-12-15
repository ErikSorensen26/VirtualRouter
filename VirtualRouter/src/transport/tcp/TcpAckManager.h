// TcpAckManager.h

#ifndef TCP_ACK_MANAGER_H
#define TCP_ACK_MANAGER_H

#include <TcpSegment.hpp>
#include <chrono>

class TimeManager;

namespace TCP
{
class TcpAckManager
{
public:
    TcpAckManager(TimeManager& tmgr);

    void reset() noexcept;
    
    void onSegmentReceived(const TcpSegment& seg, bool inOrder) noexcept;
    void requestImmediate() noexcept;
    void onAckSent(uint32_t ackNo) noexcept;

    bool ackPending() const noexcept;
    bool immediateAckPending() const noexcept;

    bool isDelayedAck() const noexcept;
    void startDelayedAck(std::chrono::steady_clock::time_point deadline) noexcept;
    void cancelDelayedAck() noexcept;

private:
    bool pending{false};
    bool immediate{false};
    TcpAck lastAckSent{0};
    uint32_t delayedId{0};

    TimeManager& tmgr;
};
} // namespace TCP

#endif // TCP_ACK_MANAGER_H
