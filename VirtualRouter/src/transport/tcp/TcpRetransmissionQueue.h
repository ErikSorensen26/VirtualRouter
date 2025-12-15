// TcpRetransmissionQueue.h

#ifndef TCP_RETRANSMISSION_QUEUE_H
#define TCP_RETRANSMISSION_QUEUE_H

#include <TcpSegment.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace TCP
{
class TcpConnection;
class TcpRetransmissionQueue
{
public:
    TcpRetransmissionQueue(TcpConnection& conn);

    struct Entry final
    {
        TcpSeq seqBegin{0};
        TcpSeq seqEnd{0};
        TcpFlags flags{0};
        TcpOptions options{};
        std::vector<uint8_t> payload{};

        std::chrono::steady_clock::time_point sentAt;
        uint32_t transmissions{0};
    };

    void clear();
    void onSegmentSent(const TcpSegment& seg);
    size_t onAckReceived(TcpAck ackNo);
    const Entry* oldest() const noexcept;
    const Entry* findBySeq(TcpSeq seq) const noexcept;
    size_t bytesOutstanding() const noexcept;

    std::optional<TcpSegment> buildRetransmission(const Entry& e, size_t maxPayload) const;

private:
    std::vector<Entry> entries;
    TcpConnection& conn;
};
}

#endif // TCP_RETRANSMISSION_QUEUE_H
