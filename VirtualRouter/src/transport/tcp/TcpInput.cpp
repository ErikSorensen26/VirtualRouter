// TcpInput.cpp
#include "TcpInput.h"
#include "Tcp.h"

namespace TCP {

TcpInput::TcpInput(TcpStack& stack) noexcept
    : stack(stack) {}

void TcpInput::onSegment(const TcpHeader& seg) {
    //TODO
    stack.input(seg);
}

} // namespace tcp
