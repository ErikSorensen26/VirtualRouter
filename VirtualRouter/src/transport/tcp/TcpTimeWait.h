// TcpTimeWait.h

#ifndef TCP_TIME_WAIT_H
#define TCP_TIME_WAIT_H

#include <TcpSocketKey.hpp>
#include <TcpSegment.hpp>

#include <chrono>
#include <unordered_map>

class TimeManager;

namespace TCP
{
class TcpTimeWait
{
public:
    TcpTimeWait(TimeManager& tmgr);
    ~TcpTimeWait();

    struct Entry final
    {
        TcpSocketKey key{};
        TcpAck lastAck{0};
        uint32_t id;
    };

    void clear();

    void insert(const TcpSocketKey& key, TcpAck lastAck, std::chrono::steady_clock::time_point expiresAt);
    bool contains(const TcpSocketKey& key) const;

    std::optional<TcpSegment> maybeAckForIncoming(const TcpSegment& in, uint16_t windowField) const;
    
private:
    TimeManager& tmgr;
    std::unordered_map<TcpSocketKey, Entry> table;
};
}

#endif // TCP_TIME_WAIT_H
