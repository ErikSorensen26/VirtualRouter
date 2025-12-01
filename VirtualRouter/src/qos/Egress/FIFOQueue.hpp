// FIFOQueEgressBase

#ifndef FIFO_QUEUE_HPP
#define FIFO_QUEUE_HPP

#include "BaseQueue.h"
#include <PacketSlot.hpp>
#include <cstdint>
#include <stdexcept>
#include <cstring>

class FIFOQueue : public BaseQueue
{
public:
    FIFOQueue(uint32_t capacity, EgressBase& egress)
      : BaseQueue(egress),
        capacity(capacity),
        mask(capacity - 1),
        head(0),
        tail(0),
        size(0)
    {
        if ((capacity & (capacity - 1)) != 0)
            throw std::runtime_error("FIFOQueue capacity must be a power of 2");

        buffer = new PacketSlot*[capacity];
        size_t size = sizeof(PacketSlot*) * capacity;
        std::memset(buffer, 0, size);
    }

    ~FIFOQueue() override
    {
        delete[] buffer;
    }

protected:
    // Called by BaseQueue inside spinlock
    void atomicEnqueue(PacketSlot* pkt) override
    {
        if (size.load(std::memory_order_acquire) >= capacity)
        {
            drop(pkt->index);
            return;
        }

        uint32_t h = head.load(std::memory_order_relaxed);
        buffer[h & mask] = pkt;
        std::atomic_thread_fence(std::memory_order_release);
        head.store(h + 1, std::memory_order_release);
        size.fetch_add(1, std::memory_order_release);
    }

    bool isEmpty() const override
    {
        return size.load(std::memory_order_acquire) == 0;
    }

    void dequeueOne() override
    {
        uint32_t t = tail.load(std::memory_order_relaxed);
        PacketSlot* pkt = buffer[t & mask];
        if (!pkt) return;

        dequeue(pkt->index, pkt->len);
        buffer[t & mask] = nullptr;

        tail.store(t + 1, std::memory_order_release);
        size.fetch_sub(1, std::memory_order_release);
    }

private:
    const uint32_t capacity;
    const uint32_t mask;
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    std::atomic<uint32_t> size;
    PacketSlot** buffer;
};

#endif // FIFO_QUEUE_HPP
