// NullEgress.cpp

#include <cstdlib>
#include <new>

#include "NullEgress.h"
#include "hardware/PacketSlot.hpp"
#include "qos/egress/TxQueueOpts.hpp"

namespace hardware::egress
{
static inline size_t alignUp(size_t v, size_t align)
{
    return (v + align - 1) & ~(align - 1);
}

NullEgress::NullEgress(interface::Interface& iface, const qos::egress::TxQueueOpts& opts)
    : EgressBase(iface, opts)
{
    frameCount = opts.frameCount ? opts.frameCount : 64;
    const size_t rawPerFrame = packetSize + MTU_PADDING + (alignof(PacketSlot) - 1) + sizeof(PacketSlot);

    frameStride = alignUp(rawPerFrame, 64);

    const size_t totalBytes = frameStride * frameCount;
    void* mem = nullptr;
    if (posix_memalign(&mem, 64, totalBytes) != 0 || !mem)
        throw std::bad_alloc();

    frameArea = static_cast<uint8_t*>(mem);

    initFreeRing(frameCount);
}

NullEgress::~NullEgress()
{
    if (frameArea)
        std::free(frameArea);
}

void NullEgress::mapFrame(uint32_t index, FrameHandle& out)
{
    if (!frameArea || index >= frameCount)
    {
        out.payload = nullptr;
        out.slot = nullptr;
        out.qid = qid;
        return;
    }

    uint8_t* base = frameArea + static_cast<size_t>(index) * frameStride;

    out.payload = base;
    out.slot = nullptr; // Egress base will set this
    out.qid = qid;
}

bool NullEgress::send(uint32_t index, uint32_t) noexcept
{
    if (index < frameCount)
        pushFree(index);
    return true;
}

void NullEgress::cancel(uint32_t index)
{
    if (index < frameCount)
        pushFree(index);
}
} // namespace hardware::egress
