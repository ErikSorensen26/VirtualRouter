// IngressBase.cpp

#include <pthread.h>
#include <sched.h>
#include <Likely.hpp>
#include <RCU.hpp>

#include "IngressBase.h"
#include "interface/Interface.h"

namespace hardware::ingress
{

IngressBase::IngressBase(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts)
    : opts(opts)
    , iface(iface)
    , qid(static_cast<uint32_t>(opts.cpuId < 0 ? 0 : opts.cpuId))
    , returnCount(0)
{}

IngressBase::~IngressBase()
{
    // stop() must be called before delete — stopRx() is pure virtual and
    // cannot be dispatched once the derived-class vtable has been replaced.
    // RxQueueManager::stopAndDelete always calls stop() first; this is a
    // safety net in case it wasn't, to at least avoid a dangling thread.
    running.store(false, std::memory_order_release);
    if (ingressThread.joinable())
        ingressThread.join();
}

void IngressBase::start()
{
    running.store(true, std::memory_order_release);
    ingressThread = std::thread([this] {
        utils::RCU::registerThread();

        // Pin to the requested CPU for cache locality.
        if (opts.cpuId >= 0)
        {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(opts.cpuId, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }

        runLoop();
        utils::RCU::unregisterThread();
    });
}

void IngressBase::stop()
{
    running.store(false, std::memory_order_release);
    stopRx();
    if (ingressThread.joinable())
        ingressThread.join();
}

void IngressBase::releaseFrame(uint32_t index)
{
    returnBuf[returnCount++] = index;
    if (returnCount == RETURN_BATCH)
        flushReturns();
}

void IngressBase::flushReturns()
{
    if (returnCount == 0) return;
    for (uint32_t i = 0; i < returnCount; ++i)
        returnToDevice(returnBuf[i]);
    returnCount = 0;
    onReturnFlush();
}

void IngressBase::runLoop()
{
    FrameView frame{};

    while (running.load(std::memory_order_acquire))
    {
        // Drain up to BATCH frames before sleeping.
        constexpr uint32_t BATCH = 256;
        uint32_t drained = 0;

        for (uint32_t i = 0; i < BATCH && pollFrame(frame); ++i)
        {
            iface.processIngress(frame.payload, frame.length);
            releaseFrame(frame.index);
            ++drained;
        }

        flushReturns();

        if (drained == 0)
            waitEvent(); // block until the kernel signals new data
    }

    // Drain and return everything before joining.
    flushReturns();
    waitUntilAllFramesReleased();
}

} // namespace hardware::ingress
