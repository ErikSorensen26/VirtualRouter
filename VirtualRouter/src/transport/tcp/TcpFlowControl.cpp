// TcpFlowControl.cpp

#include "TcpFlowControl.h"

namespace TCP
{
void TcpFlowControl::reset() noexcept
{
    localShift = 0;
}

void TcpFlowControl::setLocalWindowShift(uint8_t shift) noexcept
{
    localShift = shift;
}

uint16_t TcpFlowControl::windowFieldFromFreeSpace(size_t freeSpaceBytes) const noexcept
{
    if (freeSpaceBytes == 0) return 0;

    size_t v = (localShift == 0) ? freeSpaceBytes : (freeSpaceBytes >> localShift);
    if (v == 0) v = 1;
    if (v > 0xFFFFu) v = 0xFFFFu;
    return static_cast<uint16_t>(v);
}

uint32_t TcpFlowControl::peerWindowBytes(uint16_t winField, uint8_t peerShift) noexcept
{
    return uint32_t(winField) << peerShift;
}
} // namespace TCP
