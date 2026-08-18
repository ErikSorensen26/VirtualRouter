/**
 * @file FIFOQueue.hpp
 * @ingroup QOS_EGRESS
 */

// FIFOQueue.hpp

#ifndef FIFO_QUEUE_HPP
#define FIFO_QUEUE_HPP

#include <cstdint>
#include <stdexcept>
#include <atomic>

#include "BaseQueue.h"
#include "hardware/PacketSlot.hpp"

namespace qos::egress
{

// Lock-free FIFO queue built on a classic MPSC bounded ring.
//
// Algorithm (Dmitry Vyukov / sequence-number variant)
// ---------------------------------------------------
//   Each slot carries an atomic sequence counter initialised to its index.
//   Producers (multiple threads):
//     1. CAS tail to claim a slot index.
//     2. Spin-wait until slot.seq == claimed_pos  (another producer may be
//        mid-write into an earlier slot — wait for it).
//        In practice this spin is extremely rare because the ring is sized
//        generously relative to the number of producers.
//     3. Write pkt, release-store seq = pos + 1.
//   Consumer (single thread — BaseQueue::runLoop):
//     1. Peek at slots[head].seq; if != head + 1, queue is empty.
//     2. Read pkt, release-store seq = head + capacity (recycles the slot).
//     3. Increment head (plain, no atomic — single consumer).
//
// Full detection: if seq < pos (negative difference), all capacity slots
// have been claimed but not yet consumed — the ring is full.  The frame
// is dropped (BaseQueue::enqueue calls EgressBase::cancel).
class FIFOQueue : public BaseQueue
{
public:
    FIFOQueue(uint32_t capacity, EgressBase& egress)
        : BaseQueue(egress)
        , cap(capacity)
        , mask(capacity - 1)
    {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0)
            throw std::runtime_error("FIFOQueue: capacity must be a non-zero power of 2");

        slots = new Slot[cap];
        for (uint32_t i = 0; i < cap; ++i)
            slots[i].seq.store(i, std::memory_order_relaxed);
    }

    ~FIFOQueue() override
    {
        delete[] slots;
    }

protected:
    // ---- producer side (called from any thread) --------------------------

    bool tryEnqueue(hardware::PacketSlot* pkt) override
    {
        uint32_t pos = tail.load(std::memory_order_relaxed);

        for (;;)
        {
            Slot&    s   = slots[pos & mask];
            uint32_t seq = s.seq.load(std::memory_order_acquire);
            int32_t  diff = static_cast<int32_t>(seq) - static_cast<int32_t>(pos);

            if (diff == 0)
            {
                // Slot is free — try to claim it.
                if (tail.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed))
                    break; // claimed; pos holds our slot index
                // CAS failed (another producer won); retry with updated pos.
            }
            else if (diff < 0)
            {
                // seq < pos: the consumer hasn't recycled this slot yet.
                // Ring is full — drop.
                return false;
            }
            else
            {
                // seq > pos: tail is stale; reload.
                pos = tail.load(std::memory_order_relaxed);
            }
        }

        // We own slots[pos & mask]; write and publish.
        Slot& s  = slots[pos & mask];
        s.pkt    = pkt;
        s.seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    // ---- consumer side (called ONLY from BaseQueue::runLoop) -------------

    hardware::PacketSlot* tryDequeue() override
    {
        Slot& s = slots[head & mask];
        if (s.seq.load(std::memory_order_acquire) != head + 1)
            return nullptr; // empty or producer mid-write

        hardware::PacketSlot* pkt = s.pkt;
        // Recycle: seq = head + cap signals this slot is available again.
        s.seq.store(head + cap, std::memory_order_release);
        ++head; // plain increment — only one consumer
        return pkt;
    }

    bool isEmpty() const override
    {
        // Safe to call from the consumer thread only.
        return slots[head & mask].seq.load(std::memory_order_acquire) != head + 1;
    }

private:
    // Align each slot to a cache line to avoid false sharing between
    // producers writing seq/pkt in adjacent slots.
    struct alignas(64) Slot
    {
        std::atomic<uint32_t>  seq{0};
        hardware::PacketSlot*  pkt = nullptr;
    };

    const uint32_t cap;
    const uint32_t mask;

    // Producers increment tail via CAS.
    alignas(64) std::atomic<uint32_t> tail{0};

    // Consumer increments head as a plain integer (single-consumer guarantee).
    alignas(64) uint32_t head{0};

    Slot* slots = nullptr;
};

} // namespace qos::egress

#endif // FIFO_QUEUE_HPP
