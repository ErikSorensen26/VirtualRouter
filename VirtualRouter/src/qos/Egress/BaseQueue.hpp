// BaseQueue.hpp

#ifndef BASE_QUEUE_HPP
#define BASE_QUEUE_HPP

#include <atomic>
#include <PacketSlot.hpp>

class BaseQueue
{
public:
    inline void enqueue(FrameHandle* pkt)
    {
        while (true)
        {
            uint32_t expected = lock.load(std::memory_order_acquire);
            if (expected == 0 &&
                lock.compare_exchange_weak(expected, 1, std::memory_order_acq_rel))
            {
                break;
            }
        }

        // Critical section
        atomicEnqueue(pkt);

        // Release "lock"
        lock.store(0, std::memory_order_release);
    }

protected:
    BaseQueue() : lock(0) {}

private:
    std::atomic<uint32_t> lock;

    virtual void atomicEnqueue(FrameHandle* pkt) = 0;
};

#endif // BASE_QUEUE_HPP
