// BaseQueue.h

#ifndef BASE_QUEUE_H
#define BASE_QUEUE_H

#include <atomic>
#include <thread>
#include <linux/futex.h>
#include <sys/syscall.h>

#include "hardware/PacketSlot.hpp"
#include "hardware/egress/EgressBase.h"

namespace qos::egress
{

using EgressBase = hardware::egress::EgressBase;

// Abstract base for all TX queues.
//
// A BaseQueue owns one EgressBase (the hardware send path) and one worker
// thread.  Protocol code calls enqueue() from any thread; the worker thread
// drains the queue, calls EgressBase::send(), and flushes the batch to the
// kernel.
//
// Thread model
// ------------
//   Producers : any number of threads calling enqueue()
//   Consumer  : exactly one — the internal runThread
//
// Subclasses implement the queue data structure via three virtuals:
//   tryEnqueue(pkt)  — lock-free push; return false if full (packet is dropped)
//   tryDequeue()     — single-consumer pop; return nullptr if empty
//   isEmpty()        — true if the queue has no items (consumer-side only)
//
// Backpressure: if tryEnqueue() returns false, the frame is cancelled
// (returned to the free ring) rather than blocking the producer.
class BaseQueue
{
public:
    explicit BaseQueue(EgressBase& egress);
    virtual ~BaseQueue();

    // Thread-safe enqueue from any producer thread.
    // Drops (cancels frame) if the queue is full.
    void enqueue(hardware::PacketSlot* pkt);

    void start();
    void stop();

    EgressBase& out;

protected:
    // ---- subclass interface ----

    // Attempt to push pkt into the queue.  Must be safe to call from multiple
    // threads concurrently.  Return false if the queue is full.
    virtual bool tryEnqueue(hardware::PacketSlot* pkt) = 0;

    // Pop one packet.  Called ONLY from the consumer thread.
    // Returns nullptr if empty.
    virtual hardware::PacketSlot* tryDequeue() = 0;

    // True if the queue has no items.  Called ONLY from the consumer thread.
    virtual bool isEmpty() const = 0;

private:
    // wakeSignal == 0  → consumer is (or may be) sleeping
    // wakeSignal == 1  → consumer should drain
    alignas(64) std::atomic<uint32_t> wakeSignal{1};
    alignas(64) std::atomic<bool>     running{false};
    std::thread runThread;

    void runLoop();
    void futex_wait(std::atomic<uint32_t>* addr, uint32_t expected);
    void futex_wake(std::atomic<uint32_t>* addr, int count);
};

} // namespace qos::egress

#endif // BASE_QUEUE_H
