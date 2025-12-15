// TcpFlowControl.h

#ifndef TCP_FLOW_CONTROL_H
#define TCP_FLOW_CONTROL_H

#include <cstddef>
#include <cstdint>

namespace TCP
{
class TcpFlowControl
{
public:
    void reset() noexcept;
    void setLocalWindowShift(uint8_t shift) noexcept;

    uint16_t windowFieldFromFreeSpace(size_t freeSpaceBytes) const noexcept;
    static uint32_t peerWindowBytes(uint16_t windField, uint8_t peerShift) noexcept;

private:
    uint8_t localShift{0};
};
}

#endif // TCP_FLOW_CONTROL_H
