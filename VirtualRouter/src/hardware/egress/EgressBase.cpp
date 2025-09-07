// EgressBase.cpp

#include "EgressBase.h"
#include <pthread.h>
#include <sched.h>
#include <Interface.h>
#include <TxQueueOpts.hpp>

static inline uint32_t ceilPow2(uint32_t v)
{
    if (v <= 1) return 1;
    v--;
    v |=  v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static inline void cpuRelax() { asm volatile("pause" ::: "memory"); }

EgressBase::EgressBase(Interface& iface, const TxQueueOpts& o)
    : iface(iface), opts(o), qid(static_cast<uint32_t>(opts.cpuId < 0 ? 0 : opts.cpuId)), packetSize(iface.configs.globalMtu.load(std::memory_order_relaxed))
{
    freeBuf = nullptr;
    freeCap = 0;
    freeMask = 0;
    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);
}

EgressBase::~EgressBase()
{
    destroyFreeRing();
}

void EgressBase::initFreeRing(uint32_t frameCount)
{
    destroyFreeRing();

    freeCap  = ceilPow2(frameCount);
    freeMask = freeCap - 1;

    freeBuf = new uint32_t[freeCap];
    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);

    for (uint32_t i = 0; i < frameCount; ++i)
        pushFree(i);
}

void EgressBase::destroyFreeRing()
{
    if (freeBuf) { delete[] freeBuf; freeBuf = nullptr; }
    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);
    freeCap  = 0;
    freeMask = 0;
}

bool EgressBase::tryPopFree(uint32_t& outIndex)
{
    const uint32_t head = freeHead.load(std::memory_order_relaxed);
    const uint32_t tail = freeTail.load(std::memory_order_acquire);

    if (head == tail) return false; // empty

    outIndex = freeBuf[head & freeMask];
    freeHead.store(head + 1, std::memory_order_release);
    return true;
}

void EgressBase::pushFree(uint32_t index)
{
    const uint32_t tail = freeTail.load(std::memory_order_relaxed);
    const uint32_t head = freeHead.load(std::memory_order_acquire);

    if ((tail - head) == freeCap) {
        // ring full — shouldn’t happen for a free list, but guard it
        return;
    }

    freeBuf[tail & freeMask] = index;
    freeTail.store(tail + 1, std::memory_order_release);
}

bool EgressBase::getFrame(FrameHandle& out)
{
    uint32_t idx;
    int attempts = 0;

    while (attempts < 3)
    {
        if (tryPopFree(idx))
            break;

        onAllocNudge();
        attempts++;

        if (attempts == 2)
            waitWritable();
    }

    if (attempts >= 3 && !tryPopFree(idx))
        return false;

    out = {};
    mapFrame(idx, out);
    if (!out.payload)
    {
        cancel(idx);
        return false;
    }

    out.slot = reinterpret_cast<PacketSlot*>(out.payload + MTU_PADDING + packetSize);
    out.slot->index = idx;
    return true;
}

