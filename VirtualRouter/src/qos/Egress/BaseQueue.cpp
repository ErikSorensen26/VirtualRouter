// BaseQueue.cpp

#include <BaseQueue.h>
#include <Egress.hpp>

void BaseQueue::start()
{
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
    while (true)
    {
        uint32_t expected = 0;
        if (lock.compare_exchange_weak(expected, 1, std::memory_order_acq_rel))
            break;
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
    while(true)
    {
        while(!isEmpty())
            dequeueOne();

        if (!running.load(std::memory_order_acquire))
            break;

        wakeSignal.store(0, std::memory_order_relaxed);
        futex_wait(&wakeSignal, 0);
    }
}

void BaseQueue::dequeue(uint32_t frame, uint32_t length)
{
    out.send(frame, length);
}

void BaseQueue::drop(uint32_t frame)
{
    out.releaseFrame(frame);
}

void BaseQueue::futex_wait(std::atomic<uint32_t>* addr, uint32_t expected)
{
    syscall(SYS_futex, addr, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected, nullptr, nullptr, 0);
}

void BaseQueue::futex_wake(std::atomic<uint32_t>* addr, int count)
{
    syscall(SYS_futex, addr, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count, nullptr, nullptr, 0);
}
