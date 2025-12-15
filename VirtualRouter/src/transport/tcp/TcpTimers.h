// TcpTimers.h

#ifndef TCP_TIMERS_H
#define TCP_TIMERS_H

#include <chrono>
#include <optional>

namespace TCP
{
class TcpTimers
{
public:
    void reset() noexcept;

    void startRto(std::chrono::steady_clock::time_point deadline) noexcept;
    void cancelRto() noexcept;
    void onRtoExpired() const noexcept;

    void startDelayedAck(std::chrono::steady_clock::time_point deadline) noexcept;
    void cancelDelayedAck() noexcept;
    void onDelayedAckExpired() const noexcept;

    void startTimeWait(std::chrono::steady_clock::time_point deadline) noexcept;
    void cancelTimeWait() noexcept;
    void onTimeWaitExpired() const noexcept;

    std::optional<std::chrono::steady_clock::time_point> nextDeadline() const noexcept;

private:
    std::optional<std::chrono::steady_clock::time_point> rto;
    std::optional<std::chrono::steady_clock::time_point> delayedAck;
    std::optional<std::chrono::steady_clock::time_point> timeWait;

    uint32_t rtoId{0};
    uint32_t delayedAckId{0};
    uint32_t timeWaitId{0};
};
}

#endif // TCP_TIMERS_H
