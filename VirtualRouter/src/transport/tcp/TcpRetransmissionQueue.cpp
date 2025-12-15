// TcpRetransmissionQueue.cpp

#include "TcpRetransmissionQueue.h"
#include <TcpConnection.h>

#include <algorithm>

namespace TCP
{
TcpRetransmissionQueue::TcpRetransmissionQueue(TcpConnection& conn)
    : conn(conn) {}

void TcpRetransmissionQueue::clear()
{
    entries.clear();
}

void TcpRetransmissionQueue::onSegmentSent(const TcpSegment& seg)
{
    const size_t segSpace = seg.dataLen();
    if (segSpace == 0) return;

    Entry e{};
    e.seqBegin = seg.hdr.getSequenceNumber();
    e.seqEnd   = seg.hdr.getSequenceNumber() + static_cast<uint32_t>(segSpace);
    e.flags    = seg.hdr.raw->flags;
    e.options  = seg.options;
    e.payload  = std::vector<uint8_t>(seg.payload.begin(), seg.payload.end());
    e.sentAt   = std::chrono::steady_clock::now();
    e.transmissions = 1;

    entries.push_back(std::move(e));
}

size_t TcpRetransmissionQueue::onAckReceived(uint32_t ackNo)
{
    size_t newlyAcked = 0;

    auto it = entries.begin();
    while (it != entries.end())
    {
        if (it->seqEnd <= ackNo)
        {
            newlyAcked += static_cast<size_t>(it->seqEnd - it->seqBegin);
            it = entries.erase(it);
            continue;
        }

        // Partial ACK into this entry
        if (ackNo > it->seqBegin && ackNo < it->seqEnd)
        {
            const size_t ackPart = static_cast<size_t>(ackNo - it->seqBegin);
            newlyAcked += ackPart;

            // Trim front of payload accordingly (payload only; SYN/FIN trimming is out-of scope) //TODO
            if (!it->payload.empty())
            {
                const size_t trim = std::min<size_t>(ackPart, it->payload.size());
                it->payload.erase(it->payload.begin() + static_cast<ptrdiff_t>(trim));
            }
            it->seqBegin = ackNo;
        }
        ++it;
    }
    return newlyAcked;
}

const TcpRetransmissionQueue::Entry* TcpRetransmissionQueue::findBySeq(uint32_t seq) const noexcept
{
    for (const auto& e : entries)
        if (seq >= e.seqBegin && seq < e.seqEnd) return &e;
    return nullptr;
}

std::optional<TcpSegment> TcpRetransmissionQueue::buildRetransmission(const Entry& e, std::size_t maxPayload) const
{
    auto* iface = conn.getIface();
    if (!iface) return std::nullopt;
    TcpSegment seg(*iface);
    seg.hdr.setSequenceNumber(e.seqBegin);
    writeU16(&seg.hdr.raw->flags, e.flags);
    seg.options = e.options;

    const std::size_t n = std::min<std::size_t>(e.payload.size(), maxPayload);
    seg.payload = std::span(e.payload.data(), static_cast<std::ptrdiff_t>(n));

    // NOTE: src/dst/ack/window must be filled by the connection (context-dependent).
    return seg;
}
}
