// EgressBase.cpp

#include "EgressBase.h"
#include "qos/egress/TxQueueOpts.hpp"
#include "interface/Interface.h"
#include "hardware/PacketSlot.hpp"

namespace hardware::egress
{

EgressBase::EgressBase(interface::Interface& iface, const qos::egress::TxQueueOpts& o)
    : iface(iface),
      opts(o),
      qid(static_cast<uint32_t>(opts.cpuId < 0 ? 0 : opts.cpuId)),
      packetSize(iface.configs.globalMtu.load(std::memory_order_relaxed))
{}

EgressBase::~EgressBase()
{
    destroyFreeRing();
}

void EgressBase::initFreeRing(uint32_t frameCount)
{
    destroyFreeRing();

    hwFrameCount = frameCount;
    freeCap  = ceilPow2(frameCount);
    freeMask = freeCap - 1;

    freeBuf = new uint32_t[freeCap];
    freeSeq = new std::atomic<uint32_t>[freeCap];

    for (uint32_t i = 0; i < freeCap; ++i)
        freeSeq[i].store(i, std::memory_order_relaxed);

    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);

    for (uint32_t i = 0; i < frameCount; ++i)
        pushFree(i);
}

void EgressBase::destroyFreeRing()
{
    delete[] freeBuf;
    delete[] freeSeq;
    freeBuf      = nullptr;
    freeSeq      = nullptr;
    freeCap      = 0;
    freeMask     = 0;
    hwFrameCount = 0;
    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);
}

void EgressBase::pushFree(uint32_t index)
{
    uint32_t pos = freeTail.load(std::memory_order_relaxed);
    for (;;)
    {
        auto& slotSeq = freeSeq[pos & freeMask];
        const uint32_t seq = slotSeq.load(std::memory_order_acquire);
        const int32_t  diff = static_cast<int32_t>(seq) - static_cast<int32_t>(pos);

        if (diff == 0)
        {
            // Slot is empty at this position — claim it.
            if (freeTail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                break;
            // Another producer won the CAS; pos was updated by CAS failure.
        }
        else if (diff < 0)
        {
            // Ring is full — free list should never overflow; guard and drop.
            return;
        }
        else
        {
            // Stale pos; reload.
            pos = freeTail.load(std::memory_order_relaxed);
        }
    }

    freeBuf[pos & freeMask] = index;
    freeSeq[pos & freeMask].store(pos + 1, std::memory_order_release);
}

bool EgressBase::tryPopFree(uint32_t& outIndex)
{
    uint32_t pos = freeHead.load(std::memory_order_relaxed);
    while (true)
    {
        auto& slotSeq = freeSeq[pos & freeMask];
        const uint32_t seq = slotSeq.load(std::memory_order_acquire);
        const int32_t  diff = static_cast<int32_t>(seq) - static_cast<int32_t>(pos + 1);
        if (diff == 0)
        {
            if (freeHead.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                break;
        }
        else if (diff < 0)
        {
            // Ring is empty.
            return false;
        }
        else
        {
            // Stale pos; reload.
            pos = freeHead.load(std::memory_order_relaxed);
        }
    }

    outIndex = freeBuf[pos & freeMask];
    freeSeq[pos & freeMask].store(pos + freeCap, std::memory_order_release);
    return true;
}


bool EgressBase::getFrame(FrameHandle& out)
{
    uint32_t idx;

    if (!tryPopFree(idx))
    {
        onAllocNudge();
        if (!tryPopFree(idx))
        {
            waitWritable(); // kicks kernel, waits for EPOLLOUT, then does full reclaim
            if (!tryPopFree(idx))
                return false;
        }
    }

    out = {};
    mapFrame(idx, out);
    if (!out.payload)
    {
        pushFree(idx);
        return false;
    }

    constexpr uintptr_t align = alignof(PacketSlot);
    const uintptr_t base = reinterpret_cast<uintptr_t>(out.payload) + packetSize + MTU_PADDING;
    out.slot = reinterpret_cast<PacketSlot*>((base + align - 1u) & ~(align - 1u));

    *out.slot = {};
    out.slot->index = idx;
    return true;
}

} // namespace hardware::egress
