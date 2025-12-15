// TcpOutput.h

#ifndef TCP_OUTPUT_H
#define TCP_OUTPUT_H

class VirtualRouter;
class TcpSegment;

namespace TCP
{
class TcpOutput
{
public:
    explicit TcpOutput(VirtualRouter& vrf);

    void send(const TcpSegment& seg);
};
}

#endif // TCP_OUTPUT_H
