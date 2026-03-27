/**
 * @file BaseQueue.h
 * @brief Abstract base class for all TX queues in the egress subsystem.
 */

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

/**
 * @brief Abstract base for hardware transmit queues.
 *
 * BaseQueue owns one EgressBase (the hardware send path) and one worker thread.
 * Protocol code enqueues packets concurrently, while the consumer thread drains
 * the queue and sends packets via the EgressBase interface.
 *
 * ## Thread Model
 * Producers: any number of threads calling enqueue().
 * Consumer: exactly one, internal runThread.
 *
 * ## Subclass Responsibilities
 * Subclasses implement the queue data structure via tryEnqueue(), tryDequeue(),
 * and isEmpty() methods. Backpressure is handled by dropping frames when the
 * queue is full.
 */
class BaseQueue
{
public:
    /**
     * @brief Constructs the BaseQueue with a reference to EgressBase.
     *
     * @param egress Reference to the hardware send path backend.
     */
    explicit BaseQueue(EgressBase& egress);

    /**
     * @brief Destructs the BaseQueue.
     *
     * Stops the consumer thread and ensures all resources are cleaned up.
     */
    virtual ~BaseQueue();

    /**
     * @brief Thread-safe enqueue from any producer thread.
     *
     * Attempts to push pkt into the queue. If the queue is full, the frame is
     * cancelled via EgressBase and dropped.
     *
     * @param pkt Packet slot to enqueue.
     */
    void enqueue(hardware::PacketSlot* pkt);

    /**
     * @brief Starts the consumer thread.
     *
     * Pins the thread to the same CPU as the egress backend to maintain cache
     * locality. Begins processing the queue in runLoop().
     */
    void start();

    /**
     * @brief Stops the consumer thread.
     *
     * Wakes the thread if sleeping, sets running to false, and joins.
     */
    void stop();

    EgressBase& out; ///< Reference to the hardware send path.

protected:
    // ---- Subclass interface ----

    /**
     * @brief Attempts to push pkt into the queue.
     *
     * Must be safe for concurrent producer threads.
     * Return false if the queue is full (backpressure).
     *
     * @param pkt Packet to enqueue.
     * @return True if successful, false if queue full.
     */
    virtual bool tryEnqueue(hardware::PacketSlot* pkt) = 0;

    /**
     * @brief Pops one packet from the queue.
     *
     * Called only by the consumer thread. Returns nullptr if empty.
     *
     * @return PacketSlot* or nullptr if empty.
     */
    virtual hardware::PacketSlot* tryDequeue() = 0;

    /**
     * @brief Checks whether the queue is empty.
     *
     * Called only by the consumer thread.
     *
     * @return True if no items are present, false otherwise.
     */
    virtual bool isEmpty() const = 0;

private:
    alignas(64) std::atomic<uint32_t> wakeSignal{1}; ///< 0 = consumer may sleep, 1 = drain requested
    alignas(64) std::atomic<bool> running{false};    ///< Consumer loop running flag
    std::thread runThread;                            ///< Consumer thread

    /**
     * @brief Main consumer loop for the queue.
     *
     * Drains the queue, calls EgressBase::send(), flushes batches, and waits
     * on futex if no work is present.
     */
    void runLoop();

    /**
     * @brief Waits on a futex until addr != expected.
     *
     * Wraps SYS_futex FUTEX_WAIT call with EINTR retry.
     *
     * @param addr Atomic address to wait on.
     * @param expected Expected value for futex wait.
     */
    void futex_wait(std::atomic<uint32_t>* addr, uint32_t expected);

    /**
     * @brief Wakes up threads waiting on a futex.
     *
     * @param addr Atomic address to wake.
     * @param count Maximum number of waiters to wake.
     */
    void futex_wake(std::atomic<uint32_t>* addr, int count);
};
} // namespace qos::egress

#endif // BASE_QUEUE_H
