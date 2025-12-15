// TcpInput.h

#ifndef TCP_INPUT_H
#define TCP_INPUT_H

#include <TcpSegment.hpp>
#include <chrono>

namespace TCP
{
class TcpStack;
class TcpInput
{
public:
    explicit TcpInput(TcpStack& stack) noexcept;
    void onSegment(const TcpHeader& seq);

private:
    TcpStack& stack;
};
} // namespace TCP

#endif // TCP_INPUT_H
