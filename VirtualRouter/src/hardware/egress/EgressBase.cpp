// EgressBase.cpp

#include "EgressBase.h"
#include <pthread.h>
#include <sched.h>
#include <Interface.h>
#include <TxQueueOpts.hpp>
#include "EgressBase.h"

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
    freeSeq = nullptr;
    freeCap = 0;
    freeMask = 0;
    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);
}

EgressBase::~EgressBase()
{
    destroyBusyRing();
    destroyFreeRing();
}

void EgressBase::initFreeRing(uint32_t frameCount)
{
    destroyFreeRing();

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

    initBusyRing(frameCount);
}

void EgressBase::initBusyRing(uint32_t frameCount)
{
    destroyBusyRing();

    busyCap = ceilPow2(frameCount);
    busyMask = busyCap -1;

    busyBuf = new uint32_t[busyCap];
    busySeq = new std::atomic<uint32_t>[busyCap];
    
    for (uint32_t i = 0; i < busyCap; ++i)
        busySeq[i].store(i, std::memory_order_relaxed);
    busyHead.store(0, std::memory_order_relaxed);
    busyTail.store(0, std::memory_order_relaxed);
}

void EgressBase::destroyFreeRing()
{
    if (freeBuf) { delete[] freeBuf; freeBuf = nullptr; }
    if (freeSeq) { delete[] freeSeq; freeSeq = nullptr; }
    freeHead.store(0, std::memory_order_relaxed);
    freeTail.store(0, std::memory_order_relaxed);
    freeCap  = 0;
    freeMask = 0;
}

void EgressBase::destroyBusyRing()
{
    if (busyBuf) { delete[] busyBuf; busyBuf = nullptr; }
    if (freeSeq) { delete[] busySeq; busySeq = nullptr; }
    busyHead.store(0, std::memory_order_relaxed);
    busyTail.store(0, std::memory_order_relaxed);
    busyCap = 0;
    busyCap = 0;
}

bool EgressBase::tryPopFree(uint32_t& outIndex)
{
    uint32_t head = freeHead.load(std::memory_order_relaxed);
    while (true)
    {
        uint32_t slot = head & freeMask;
        uint32_t seq  = freeSeq[slot].load(std::memory_order_acquire);
        int32_t  diff = (int32_t)seq - (int32_t)(head + 1);  // <-- head + 1

        if (diff == 0)
        {
            if (freeHead.compare_exchange_weak(
                    head, head + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                outIndex = freeBuf[slot];
                freeSeq[slot].store(head + freeCap, std::memory_order_release);
                return true;
            }
            continue;
        }
        else if (diff < 0)
        {
            return false;
        }
        else
        {
            head = freeHead.load(std::memory_order_relaxed);
        }
    }
}

bool EgressBase::tryPopBusy(uint32_t& outIndex)
{
    uint32_t head = busyHead.load(std::memory_order_relaxed);
    while (true)
    {
        uint32_t slot = head & busyMask;
        uint32_t seq = busySeq[slot].load(std::memory_order_acquire);
        int32_t diff = (int32_t)seq - (int32_t)(head + 1);
        if (diff == 0)
        {
            if (busyHead.compare_exchange_weak(
                    head, head + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                outIndex = busyBuf[slot];
                busySeq[slot].store(head + busyCap, std::memory_order_release);
                return true;
            }
            continue;
        }
        else if (diff < 0)
        {
            return false;
        }
        else
        {
            head = busyHead.load(std::memory_order_relaxed);
        }
    }
}

void EgressBase::pushFree(uint32_t index)
{
    uint32_t tail = freeTail.load(std::memory_order_relaxed);
    while (true)
    {
        uint32_t slot = tail & freeMask;
        uint32_t seq  = freeSeq[slot].load(std::memory_order_acquire);
        int32_t  diff = (int32_t)seq - (int32_t)tail;

        if (diff == 0)
        {
            if (freeTail.compare_exchange_weak(
                    tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                freeBuf[slot] = index;
                freeSeq[slot].store(tail + 1, std::memory_order_release);
                return;
            }
            continue;
        }
        else if (diff < 0)
        {
            cpuRelax();
        }
        else
        {
            tail = freeTail.load(std::memory_order_relaxed);
        }
    }
}

void EgressBase::pushBusy(uint32_t index)
{
    uint32_t tail = busyTail.load(std::memory_order_relaxed);
    while (true)
    {
        uint32_t slot = tail & busyMask;
        uint32_t seq = busySeq[slot].load(std::memory_order_acquire);
        int32_t diff = (int32_t)seq - (int32_t)tail;
        if (diff == 0)
        {
            if (busyTail.compare_exchange_weak(
                    tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                busyBuf[slot] = index;
                busySeq[slot].store(tail + 1, std::memory_order_release);
                return;
            }
            continue;
        }
        else if (diff < 0)
        {
            cpuRelax();
        }
        else
        {
            tail = busyTail.load(std::memory_order_relaxed);
        }
    }
}

bool EgressBase::getFrame(FrameHandle& out)
{
    uint32_t idx;
    if (!tryPopFree(idx))
    {
        onAllocNudge();             // backend can reclaim/kick
        if (!tryPopFree(idx))
            return false;
    }

    out = {};
    mapFrame(idx, out);
    if (!out.payload)
    {
        pushFree(idx);
        return false;
    }

    out.slot = reinterpret_cast<PacketSlot*>(out.payload + MTU_PADDING + packetSize);
    out.slot->index = idx;
    return true;
}

