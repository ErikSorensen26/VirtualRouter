// TcpState.hpp

#ifndef TCP_STATE_HPP
#define TCP_STATE_HPP

#include <cstdint>

namespace TCP
{
enum class TcpState : uint8_t
{
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT
};

enum class TcpShutdown : uint8_t
{
    READ,
    WRITE,
    READ_WRITE
};
}

#endif // TCP_STATE_HPP
