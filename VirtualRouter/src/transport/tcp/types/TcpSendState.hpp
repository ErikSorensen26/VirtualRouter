// TcpSendState.hpp

#ifndef TCP_SEND_STATE_HPP
#define TCP_SEND_STATE_HPP

#include <TcpSegment.hpp>
#include <cstdint>

namespace TCP
{
struct TcpSendState final
{
    TcpSeq iss{0};
    TcpSeq sndUna{0};
    TcpSeq sndNxt{0};

    uint32_t sndWndBytes{0};
    uint32_t peerWndShift{0};

    TcpSeq sndWl1{0};
    TcpSeq sndWl2{0};

    void reset() noexcept
    {
        iss = 0;
        sndUna = 0;
        sndNxt = 0;
        sndWndBytes = 0;
        peerWndShift = 0;
        sndWl1 = 0;
        sndWl2 = 0;
    }

    void updatePeerWindow(uint16_t winField, TcpSeq segSeq, TcpAck segAck) noexcept
    {
        if (segSeq > sndWl1 || (segSeq == sndWl1 && (segAck == sndWl2 || segAck >= sndWl2)))
        {
            sndWl1 = segSeq;
            sndWl2 = segAck;
            sndWndBytes = uint32_t(winField) << peerWndShift;
        }
    }
    uint32_t bytesInFlight() const noexcept
    {
        return uint32_t(sndNxt - sndUna);
    }
};
}

#endif // TCP_SEND_STATE_HPP
