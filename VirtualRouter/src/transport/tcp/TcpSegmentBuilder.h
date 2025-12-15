// TcpSegmentBuilder.h

#ifndef TCP_SEGMENT_BUILDER_H
#define TCP_SEGMENT_BUILDER_H

#include <TcpSegment.hpp>
#include <variant>

class Interface;

namespace TCP
{
class TcpSegmentBuilder
{
public:
    struct Params final
    {
        Params(Interface& iface) : inputObj({&iface, false}) {}
        Params(TcpHeader& hdr) : inputObj({&hdr, true}) {}

        Params(Interface& iface, const TcpEndpoint& src, const TcpEndpoint& dst, TcpSeq seq, TcpAck ack, uint16_t wf, uint16_t up, TcpOptions opts = {})
            : inputObj({&iface, false}), src(src), dst(dst), seq(seq), ack(ack), windowField(wf), urgentPointer(up), options(opts) {}
        Params(TcpHeader& hdr, const TcpEndpoint& src, const TcpEndpoint& dst, TcpSeq seq, TcpAck ack, uint16_t wf, uint16_t up, TcpOptions opts = {})
            : inputObj({&hdr, true}), src(src), dst(dst), seq(seq), ack(ack), windowField(wf), urgentPointer(up), options(opts) {}

        std::pair<void*, bool> inputObj;
        TcpEndpoint src{};
        TcpEndpoint dst{};
        TcpSeq seq{0};
        TcpSeq ack{0};
        uint16_t windowField{0};
        uint16_t urgentPointer{0};
        TcpOptions options{};
        std::span<const uint8_t> payload{};
    };

    static TcpSegment build(const Params& p);

    static TcpSegment buildPureAck(Interface& iface, const TcpEndpoint& src, const TcpEndpoint& dst, TcpSeq seq, TcpAck ack, uint16_t windowField, TcpOptions options = {});

    static TcpSegment buildRst(Interface& iface, const TcpEndpoint& src, const TcpEndpoint& dst, TcpSeq seq, TcpAck ack, bool setAckField);
};
} // namespace TCP

#endif // TCP_SEGMENT_BUILDER_H
