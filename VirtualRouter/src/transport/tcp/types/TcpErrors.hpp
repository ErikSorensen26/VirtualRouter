// TcpErrors.hpp

#ifndef TCP_ERRORS_HPP
#define TCP_ERRORS_HPP

#include <TcpSegment.hpp>
#include <cstdint>

namespace TCP
{
class TcpErrors
{
public:
    static TcpSegment makeRstForIncoming(const TcpSegment& in, uint16_t windowField = 0)
    {
    TcpSegment out{};
    out.src = in.dst;
    out.dst = in.src;
    out.hdr.setWindowSize(0);

    if (in.hdr.getFlagACK()) {
        // RST in response to an ACK: SEQ = incoming ACK, no ACK flag set.
        out.hdr.setSequenceNumber(in.hdr.getAckNumber());
        out.hdr.setAckNumber(0);
        out.flags = static_cast<TcpFlags>(TcpFlag::Rst);
    } else {
        // Otherwise: RST+ACK with ACK = SEQ + dataLen
        out.seq = 0;
        out.ack = in.seq + static_cast<TcpAck>(in.dataLen());
        out.flags = static_cast<TcpFlags>(TcpFlag::Rst) | TcpFlag::Ack;
    }

    return out;
    }

    static TcpHeader makeAck(const TcpSegment& in, TcpSeq seq, TcpAck ack, uint16_t windowField, TcpOptions options = {})
    {
        TcpSegment out{};
        out.src = in.dst;
        out.dst = in.src;
        out.seq = seq;
        out.ack = ack;
        out.flags = static_cast<TcpFlags>(TcpFlag::Ack);
        out.window = windowField;
        out.options = std::move(options);
        return out;
    }
};
} // namespace tcp

#endif // TCP_ERRORS_HPP
