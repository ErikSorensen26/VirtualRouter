// TcpSegmentBuilder.cpp

#include "TcpSegmentBuilder.h"

namespace TCP
{
TcpSegment TcpSegmentBuilder::build(const Params& p)
{
    TcpSegment seg = [&]{
        if (p.inputObj.second)
            return TcpSegment(*static_cast<Interface*>(p.inputObj.first));
        else
            return TcpSegment(*static_cast<TcpHeader*>(p.inputObj.first));
    }();

    seg.src = p.src;
    seg.hdr.setSourcePort(p.src.port);
    seg.dst = p.dst;
    seg.hdr.setDestinationPort(p.dst.port);
    seg.hdr.setSequenceNumber(p.seq);
    seg.hdr.setAckNumber(p.ack);
    seg.hdr.setWindowSize(p.windowField);
    seg.hdr.setUrgentPointer(p.urgentPointer);
    seg.options = p.options;
    seg.payload = p.payload;
    return seg;
}

TcpSegment TcpSegmentBuilder::buildPureAck(Interface& iface, const TcpEndpoint& src, const TcpEndpoint& dst, TcpSeq seq, TcpAck ack, uint16_t windowField, TcpOptions options)
{
    Params p(iface);
    p.src = src;
    p.dst = dst;
    p.seq = seq;
    p.ack = ack;
    p.windowField = windowField;
    //p.options = std::move(options);
    TcpSegment tcp = build(p);
    tcp.hdr.setFlagACK(true);
    return tcp;
}

TcpSegment TcpSegmentBuilder::buildRst(Interface& iface, const TcpEndpoint& src, const TcpEndpoint& dst, TcpSeq seq, TcpAck ack, bool setAckFlag)
{
    Params p(iface);
    p.src = src;
    p.dst = dst;
    p.seq = seq;
    p.ack = ack;
    p.windowField = 0;
    TcpSegment tcp = build(p);
    tcp.hdr.setFlagRST(true);
    if (setAckFlag)
        tcp.hdr.setFlagACK(true);
    return tcp;
}
}
