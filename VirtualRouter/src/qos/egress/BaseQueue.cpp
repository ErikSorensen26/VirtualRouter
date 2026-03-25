// BaseQueue.cpp

#include <pthread.h>
#include <sched.h>

#include "BaseQueue.h"
#include "hardware/egress/EgressBase.h"
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

// ---- producer side -------------------------------------------------------

void BaseQueue::enqueue(hardware::PacketSlot* pkt)
{
    if (!tryEnqueue(pkt))
    {
        // Queue full — cancel the frame so its slot returns to the free ring.
        out.cancel(pkt->index);
        return;
    }

    // Wake the consumer only if it was sleeping (wakeSignal was 0).
    // exchange() avoids a separate load; if the old value was already 1
    // (consumer awake) we skip the syscall.
    if (wakeSignal.exchange(1, std::memory_order_release) == 0)
        futex_wake(&wakeSignal, 1);
}

// ---- lifecycle -----------------------------------------------------------

void BaseQueue::start()
{
    running.store(true, std::memory_order_release);
    runThread = std::thread([this] {
        // Pin to the same CPU as the egress backend so the TX ring buffer
        // stays in the same L1/L2 cache as the thread writing to it.
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
    // Unconditionally wake the consumer so it sees running==false and exits.
    wakeSignal.exchange(1, std::memory_order_release);
    futex_wake(&wakeSignal, 1);
    if (runThread.joinable())
        runThread.join();
}

// ---- consumer thread -----------------------------------------------------

void BaseQueue::runLoop()
{
    utils::RCU::registerThread();

    hardware::PacketSlot* pkt;

    while (running.load(std::memory_order_acquire))
    {
        // ── Drain phase ──────────────────────────────────────────────────
        bool sent = false;
        while ((pkt = tryDequeue()) != nullptr)
        {
            if (out.send(pkt->index, pkt->len))
                sent = true;
            else
                out.cancel(pkt->index);
        }
        // Batch-flush to kernel once per drain burst (single sendto for
        // EgressPacket, no-op for EgressSend which already sent inline).
        if (sent) out.flush();

        // ── Sleep preparation ─────────────────────────────────────────────
        // Set wakeSignal = 0 BEFORE the second drain so a racing producer
        // that writes wakeSignal = 1 after our store will either:
        //   a) be seen by the second drain (no sleep needed), or
        //   b) cause the futex_wait below to return immediately.
        wakeSignal.store(0, std::memory_order_release);

        // ── Second drain (avoid missing items enqueued between drain and sleep)
        sent = false;
        while ((pkt = tryDequeue()) != nullptr)
        {
            if (out.send(pkt->index, pkt->len))
                sent = true;
            else
                out.cancel(pkt->index);
        }
        if (sent) out.flush();

        // ── Sleep if no producer has signalled since we cleared wakeSignal ──
        uint32_t expected = 0;
        if (wakeSignal.compare_exchange_strong(expected, 0, std::memory_order_acquire))
            futex_wait(&wakeSignal, 0);
    }

    // Final drain after stop() sets running = false.
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

// ---- futex helpers -------------------------------------------------------

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
