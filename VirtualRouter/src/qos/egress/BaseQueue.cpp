// BaseQueue.cpp

#include <pthread.h>
#include <sched.h>

#include "BaseQueue.h"
#include <RCU.hpp>

namespace qos::egress
{

BaseQueue::BaseQueue(EgressBase& egress)
    : out(egress)
{}

BaseQueue::~BaseQueue()
{
    stop();
}

void BaseQueue::enqueue(hardware::PacketSlot* pkt)
{
    if (!tryEnqueue(pkt))
    {
        out.cancel(pkt->index);
        return;
    }

    if (wakeSignal.exchange(1, std::memory_order_release) == 0)
        futex_wake(&wakeSignal, 1);
}

void BaseQueue::start()
{
    running.store(true, std::memory_order_release);
    runThread = std::thread([this] {
        const int cpuId = out.getCpuId();
        if (cpuId >= 0)
        {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpuId, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }
        runLoop();
    });
}

void BaseQueue::stop()
{
    running.store(false, std::memory_order_release);
    wakeSignal.exchange(1, std::memory_order_release);
    futex_wake(&wakeSignal, 1);
    if (runThread.joinable())
        runThread.join();
}

void BaseQueue::runLoop()
{
    utils::RCU::registerThread();

    hardware::PacketSlot* pkt;

    while (running.load(std::memory_order_acquire))
    {
        bool sent = false;
        while ((pkt = tryDequeue()) != nullptr)
        {
            if (out.send(pkt->index, pkt->len))
                sent = true;
            else
                out.cancel(pkt->index);
        }
        if (sent) out.flush();

        wakeSignal.store(0, std::memory_order_release);

        sent = false;
        while ((pkt = tryDequeue()) != nullptr)
        {
            if (out.send(pkt->index, pkt->len))
                sent = true;
            else
                out.cancel(pkt->index);
        }
        if (sent) out.flush();

        uint32_t expected = 0;
        if (wakeSignal.compare_exchange_strong(expected, 0, std::memory_order_acquire))
            futex_wait(&wakeSignal, 0);
    }

    bool sent = false;
    while ((pkt = tryDequeue()) != nullptr)
    {
        if (out.send(pkt->index, pkt->len))
            sent = true;
        else
            out.cancel(pkt->index);
    }
    if (sent) out.flush();

    utils::RCU::unregisterThread();
}

void BaseQueue::futex_wait(std::atomic<uint32_t>* addr, uint32_t expected)
{
    int rc;
    do {
        rc = syscall(SYS_futex, addr, FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                     expected, nullptr, nullptr, 0);
    } while (rc == -1 && errno == EINTR);
}

void BaseQueue::futex_wake(std::atomic<uint32_t>* addr, int count)
{
    syscall(SYS_futex, addr, FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
            count, nullptr, nullptr, 0);
}
} // namespace qos::egress
