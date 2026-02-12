// BaseQueue.cpp

#include "BaseQueue.h"
#include "hardware/egress/EgressBase.h"
#include <RCU.hpp>

void BaseQueue::start()
{
    running.store(true, std::memory_order_release);
    wakeSignal.store(1, std::memory_order_relaxed);
    futex_wake(&wakeSignal, 1);

    runThread = std::thread([this] { this->runLoop(); });
}

void BaseQueue::stop()
{
    running.store(false, std::memory_order_release);
    wakeSignal.store(1, std::memory_order_relaxed);
    futex_wake(&wakeSignal, 1);

    if (runThread.joinable())
        runThread.join();
}

void BaseQueue::enqueue(PacketSlot* pkt)
{
    uint32_t backoff = 1;
    while (true)
    {
        uint32_t expected = 0;
        if (lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed))
            break;

        for (uint32_t i = 0; i < backoff; i++)
            cpuRelax();
        backoff = std::min(backoff * 2, 256u);
    }

    // Critical section
    bool wasEmpty = isEmpty();
    atomicEnqueue(pkt);

    // Release "lock"
    lock.store(0, std::memory_order_release);

    // Wake up thread to process
    if (wasEmpty)
    {
        wakeSignal.store(1, std::memory_order_relaxed);
        futex_wake(&wakeSignal, 1);
    }
}

void BaseQueue::runLoop()
{
    RCU::registerThread();
    while(running.load(std::memory_order_acquire))
    {
        while(!isEmpty())
            dequeueOne();

        uint32_t expected = 0;
        if (wakeSignal.compare_exchange_strong(expected, 0, std::memory_order_acq_rel))
            futex_wait(&wakeSignal, 0);

        while (!isEmpty()) dequeueOne();
    }
    RCU::unregisterThread();
}

void BaseQueue::dequeue(uint32_t frame, uint32_t length)
{
    static thread_local uint32_t flushCount = 0;
    if (out.send(frame, length))
    {
        if (++flushCount >= 512 || isEmpty())
        {
        }
        out.flush();
        flushCount = 0;
    }
    else
    {
        out.cancel(frame);
    }
}

void BaseQueue::drop(uint32_t frame)
{
    out.cancel(frame);
}

void BaseQueue::futex_wait(std::atomic<uint32_t>* addr, uint32_t expected)
{
    int rc;
    do {
        rc = syscall(SYS_futex, addr, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected, nullptr, nullptr, 0);
    } while (rc == -1 && errno == EINTR);
}

void BaseQueue::futex_wake(std::atomic<uint32_t>* addr, int count)
{
    syscall(SYS_futex, addr, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count, nullptr, nullptr, 0);
}
