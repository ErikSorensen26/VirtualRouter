// BaseQueue.cpp

#include <BaseQueue.h>
#include <EgressBase.h>

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

        uint32_t expected = 1;
        (void)wakeSignal.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);

        if (!isEmpty()) continue;

        futex_wait(&wakeSignal, 0);
    }
}

void BaseQueue::dequeue(uint32_t frame, uint32_t length)
{
    static thread_local uint32_t flushCount = 0;
    if (out.send(frame, length))
    {
        if (++flushCount >= 512 || isEmpty())
        {
            out.flush();
            flushCount = 0;
        }
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
    syscall(SYS_futex, addr, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected, nullptr, nullptr, 0);
}

void BaseQueue::futex_wake(std::atomic<uint32_t>* addr, int count)
{
    syscall(SYS_futex, addr, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count, nullptr, nullptr, 0);
}
