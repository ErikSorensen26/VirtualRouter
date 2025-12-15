// TcpSegment.hpp

#ifndef TCP_SEGMENT_HPP
#define TCP_SEGMENT_HPP

#include "TcpEndpoint.hpp"
#include "TcpOptions.h"
#include <TcpHeader.hpp>

#include <cstddef>
#include <cstdint>

class Interface;

namespace TCP
{
using TcpSeq = uint32_t;
using TcpAck = uint32_t;
using TcpFlags = uint16_t;

struct TcpSegment final
{
    TcpSegment(Interface& iface);
    TcpSegment(TcpHeader& hdr);

    TcpEndpoint src{};
    TcpEndpoint dst{};

    uint16_t getFlags() const noexcept { return readU16(&hdr.raw->dataOffsetAndFlags1); }

    TcpHeader hdr{};

    void buildHeader(size_t mss)
    {
        size_t optSize = options.encode(hdr.buffer + TcpHeader::fixedSize, mss);
        std::memcpy(hdr.buffer + TcpHeader::fixedSize + optSize, payload.data(), payload.size());
        hdr.setTrailSize(optSize + payload.size());
    }

    uint8_t* getOptions()
    {
        return hdr.buffer + TcpHeader::fixedSize;
    }

    void setSrc(const TcpEndpoint& te)
    {
        src = te;
        hdr.setSourcePort(te.port);
    }

    void setDst(const TcpEndpoint& te)
    {
        dst = te;
        hdr.setDestinationPort(te.port);
    }

    void setSeq(TcpSeq seq)
    {
        hdr.setSequenceNumber(seq);
    }

    void setAck(TcpSeq ack)
    {
        hdr.setAckNumber(ack);
    }

    void setWindowSize(uint16_t ws)
    {
        hdr.setWindowSize(ws);
    }

    TcpOptions options{};
    std::span<const uint8_t> payload;

    size_t payloadLen() const noexcept
    {
        return payload.size();
    }

    size_t dataLen() const noexcept
    {
        size_t n = payload.size();
        if (hdr.getFlagSYN()) n += 1;
        if (hdr.getFlagFIN()) n += 1;
        return n;
    }
};
}

#endif // TCP_SEGMENT_HPP
