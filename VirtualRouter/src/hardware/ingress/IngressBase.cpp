// IngressBase.cpp

#include <sched.h>
#include <pthread.h>
#include <likely.hpp>
#include <chrono>
#include <RCU.hpp>

#include "IngressBase.h"
#include "interface/Interface.h"

thread_local std::array<uint32_t, 64> localBatch;
thread_local size_t batchCount = 0;

static inline void cpu_relax() { asm volatile("pause" ::: "memory"); }

IngressBase::IngressBase(Interface& iface, const RxQueueOpts& opts)
    : opts(opts), iface(iface), qid((static_cast<uint32_t>(opts.cpuId < 0 ? 0 : opts.cpuId)))
{
    for (uint32_t i = 0; i < RETURN_RING_CAP; ++i)
        returnSeq[i].store(i, std::memory_order_relaxed);
}

IngressBase::~IngressBase()
{
    stop();
}

void IngressBase::start()
{
    running.store(true, std::memory_order_release);
    ingressThread = std::thread([this]{
        RCU::registerThread();
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(qid, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

        runLoop();
        RCU::unregisterThread();
    });
}

void IngressBase::stop()
{
    running.store(false, std::memory_order_release);
    stopRx();
    if (ingressThread.joinable()) ingressThread.join();
}

void IngressBase::releaseFrame(uint32_t index)
{
    /*localBatch[batchCount++] = index;
    if (batchCount == localBatch.size())
        flushLocalBatch();*/

    uint32_t pos = tail.fetch_add(1, std::memory_order_relaxed);
    uint32_t slot = pos & (RETURN_RING_CAP - 1);

    uint32_t expected = pos;
    while (returnSeq[slot].load(std::memory_order_acquire) != expected)
        asm volatile("pause");

    returnBuf[slot] = index;
    returnSeq[slot].store(pos + 1, std::memory_order_release);

    uint32_t pending = (pos + 1) - head.load(std::memory_order_relaxed);
    if (pending >= RETURN_RING_CAP - 64)
    {
        onReturnNudge();
        flushReturned(512);
    }
}

void IngressBase::flushLocalBatch()
{
    size_t n = batchCount;
    if (unlikely(n == 0)) return;

    const uint32_t mask = (RETURN_RING_CAP - 1);
    uint32_t base = tail.fetch_add((uint32_t)n, std::memory_order_acq_rel);

    for (size_t j = 0; j < n; ++j)
    {
        uint32_t pos = base + (uint32_t)j;
        uint32_t slot = pos & mask;

        while (returnSeq[slot].load(std::memory_order_acquire) != pos)
            cpu_relax();

        returnBuf[slot] = localBatch[j];
        returnSeq[slot].store(pos +1, std::memory_order_release);
    }

    onReturnNudge();
    uint32_t want = (uint32_t)std::max<size_t>(n, 2048);
    flushReturned(want);

    batchCount = 0;
}

void IngressBase::flushReturned(uint32_t maxBatch)
{
    if (returnDrainOwner.test_and_set(std::memory_order_acquire))
        return;

    uint32_t h = head.load(std::memory_order_relaxed);
    uint32_t t = tail.load(std::memory_order_acquire);

    uint32_t done = 0;
    while (done < maxBatch && h != t)
    {
        uint32_t slot = h & (RETURN_RING_CAP - 1);

        if (returnSeq[slot].load(std::memory_order_acquire) != (h + 1))
            break;

        uint32_t idx = returnBuf[slot];
        returnToDevice(idx);

        returnSeq[slot].store(h + RETURN_RING_CAP, std::memory_order_release);
        h++; ++done;
    }

    if (done) head.store(h, std::memory_order_release);
    returnDrainOwner.clear(std::memory_order_release);
}

void IngressBase::runLoop()
{
    std::chrono::steady_clock::time_point last;
    auto startTime = std::chrono::steady_clock::now();

    FrameView frame{};

    while (running.load(std::memory_order_acquire))
    {
        uint32_t drained = 0;

        uint32_t budget = 256;
        while (budget-- && pollFrame(frame))
        {
            iface.processIngress(frame.payload, frame.length);
            releaseFrame(frame.index);
            ++drained;
        }

        flushLocalBatch();
        flushReturned(std::numeric_limits<uint32_t>::max());

        if (drained == 0)
        {
            flushLocalBatch();
            waitEvent();
        }

    }
    flushLocalBatch();
    waitUntilAllFramesReleased();
}

