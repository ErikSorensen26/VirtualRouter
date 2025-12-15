// TcpTimeWait.cpp

#include "TcpTimeWait.h"
#include <TimeManager.h>

namespace TCP {

TcpTimeWait::TcpTimeWait(TimeManager& tmgr)
    : tmgr(tmgr) {}

TcpTimeWait::~TcpTimeWait()
{
    clear();
}

void TcpTimeWait::clear()
{
    for (auto& e : table)
    {
        if (e.second.id != 0)
        {
            tmgr.cancelTimer(e.second.id);
            e.second.id = 0;
        }
    }
    table.clear();
}

void TcpTimeWait::insert(const TcpSocketKey& key, TcpAck lastAck, std::chrono::steady_clock::time_point expiresAt)
{
    Entry e{};
    e.key = key;
    e.lastAck = lastAck;
    e.id = tmgr.addTimer(expiresAt, [this, key]() {
        table.erase(key);
    });
    table[key] = e;
}

bool TcpTimeWait::contains(const TcpSocketKey& key) const
{
    auto it = table.find(key);
    return it != table.end();
}

std::optional<TcpSegment> TcpTimeWait::maybeAckForIncoming(const TcpSegment& in, std::uint16_t windowField) const
{
    TcpSocketKey key{ in.dst, in.src };
    auto it = table.find(key);
    if (it == table.end()) return std::nullopt;

    // Minimal TIME-WAIT behavior: ACK what we last acknowledged.
    TcpSegment ack{};
    ack.setSrc(in.src);
    ack.setDst(in.dst);
    ack.setSeq(0); // Out-of-scope without tracking full TW sequence state here.
    ack.setAck(it->second.lastAck); 
    ack.hdr.setFlagACK(true);
    ack.setWindowSize(windowField);
    return ack;
}

} // namespace tcp
