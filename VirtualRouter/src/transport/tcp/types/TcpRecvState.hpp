// TcpRecvState.hpp

#ifndef TCP_RECV_STATE_HPP
#define TCP_RECV_STATE_HPP

#include <TcpSegment.hpp>
#include <cstdint>

namespace TCP
{
struct TcpRecvState final
{
    TcpSeq irs{0};
    TcpSeq rcvNxt{0};
    uint32_t rcvWndBytes{0};
    uint8_t localWndShift{0};

    void reset() noexcept
    {
        irs = 0;
        rcvNxt = 0;
        rcvWndBytes = 0;
        localWndShift = 0;
    }
};
}

#endif // TCP_RECV_STATE_HPP
