// TcpStateMachine.h

#ifndef TCP_STATE_MACHINE_H
#define TCP_STATE_MACHINE_H

#include <TcpSegment.hpp>
#include <chrono>

namespace TCP
{
class TcpConnection;
class TcpOutput;

class TcpStateMachine
{
public:
    static void onSegment(TcpConnection& c, const TcpSegment& seg, TcpOutput& out);
};
} // namespace TCP

#endif // TCP_STATE_MACHINE_H
