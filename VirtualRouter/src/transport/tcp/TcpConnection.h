// TcpConnection.h

#ifndef TCP_CONNECTION_H
#define TCP_CONNECTION_H

#include "TcpAckManager.h"
#include "TcpCongestionControl.h"
#include "TcpFlowControl.h"
#include "TcpLossRecovery.h"
#include "TcpRecvBuffer.h"
#include "TcpRetransmissionQueue.h"
#include "TcpSendBuffer.h"
#include "TcpTimers.h"

#include <TcpSocketKey.hpp>
#include <TcpState.hpp>
#include <TcpRecvState.hpp>
#include <TcpSendState.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <atomic>
#include <span>

class TimeManager;
class Interface;

namespace TCP
{
class TcpOutput;
class TcpConnection
{
public:
    struct Config final
    {
        size_t sendBufferBytes{256 * 1024};
        size_t recvBufferBytes{256 * 1024};

        std::chrono::steady_clock::duration delayedAckDelay{std::chrono::milliseconds(40)};
        std::chrono::steady_clock::duration timeWaitDuration{std::chrono::seconds(60)};
        std::chrono::steady_clock::duration initialRto{std::chrono::seconds(1)};

        uint16_t mss{0};
        uint8_t defaultWindowShift{7};
    };

    explicit TcpConnection(TcpSocketKey key, const Config& cfg, TimeManager& tmgr, Interface* iface);
    const TcpSocketKey& socketKey() const noexcept;
    TcpState getState() const noexcept;

    void startConnection() noexcept;
    void startFromSyn(const TcpSegment& syn);

    void onSegment(const TcpSegment& seg, TcpOutput& out);
    void onTick(TcpOutput& out);

    size_t send(std::span<const uint8_t> data);
    size_t recv(std::span<uint8_t> out);

    size_t readableBytes() const noexcept;
    size_t writableBytes() const noexcept;

    void shutdown(TcpShutdown how, TcpOutput& out);
    void close(TcpOutput& out);
    void abort(TcpOutput& out);

    bool isClosed() const noexcept;
    bool inTimeWait() const noexcept;
    std::optional<std::chrono::steady_clock::time_point> nextDeadline() const noexcept;

    uint16_t peerMssOrDefault() const noexcept;
    uint8_t peerWindowShift() const noexcept;
    uint8_t localWindowShift() const noexcept;

    Interface* getIface() { return iface.load(std::memory_order_relaxed); }

private:
    friend class TcpStateMachine;

    void flush();
    void sendAckOnly(TcpOutput& out);
    TcpSegment createControl();
    void sendControl(TcpSegment& seg, TcpOutput& out);
    void sendRstResponse(const TcpSegment& in, TcpOutput& out);

    TcpSocketKey key{};
    TcpState state{TcpState::CLOSED};

    Config cfg{};

    TcpSendState snd{};
    TcpRecvState rcv{};

    TcpSendBuffer sendBuf;
    TcpRecvBuffer recvBuf;
    TcpRetransmissionQueue rtxq;

    TcpCongestionControl cc;
    TcpLossRecovery loss;
    TcpFlowControl flow;
    TcpAckManager ackm;
    TcpTimers timers; 

    uint16_t peerMss{0};
    uint8_t peerWndShift{0};
    uint8_t localWndShift{0};

    bool finSent{false};
    bool finAcked{false};
    bool finReceived{false};

    const AddressFamily af;

    std::atomic<Interface*> iface;
    TimeManager& tmgr;
};
} // Namespace TCP

#endif // TCP_CONNECTION_H
