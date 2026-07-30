/**
 * @file IngressBase.h
 * @brief Abstract base for all hardware ingress (RX) backends.
 */

/**
 * @defgroup HARDWARE_INGRESS Ingress
 * @ingroup HARDWARE
 * @brief RX backends: IngressXdp (AF_XDP zero-copy) and IngressPacket (TPACKET_V3).
 */

#ifndef INGRESS_BASE_H
#define INGRESS_BASE_H

#include <atomic>
#include <thread>
#include <cstdint>

#include "qos/ingress/RxQueueOpts.hpp"

namespace interface { class Interface; }

/**
 * @namespace hardware::ingress
 * @brief Ingress (RX) backend implementations and the shared IngressBase.
 *
 * IngressBase owns the RX thread and batch-return logic. Concrete subclasses
 * (@ref IngressXdp, @ref IngressPacket) implement the hardware-facing side.
 * Constructed exclusively by the @ref hardware::ingress::create() factory.
 */
namespace hardware::ingress
{

/**
 * @brief A single received frame's payload pointer, byte length, and ring index.
 *
 * Returned by @ref IngressBase::pollFrame(). The @p index must be passed to
 * @ref IngressBase::releaseFrame() once the caller has finished with the
 * payload so the slot can be returned to the kernel.
 */
struct FrameView
{
    uint8_t* payload = nullptr; ///< Pointer into the kernel-shared ring buffer; do not free.
    uint32_t length  = 0;       ///< Valid bytes at @p payload.
    uint32_t index   = 0;       ///< Ring slot index; passed to releaseFrame().
};

/**
 * @brief Backend-agnostic RX base: owns the ingress thread and the batch frame-return buffer.
 * @ingroup HARDWARE_INGRESS
 *
 * IngressBase manages the RX thread lifecycle and amortises per-frame kernel
 * round-trips by buffering up to @c RETURN_BATCH frame indices before calling
 * @ref returnToDevice() in a single flush. Subclasses only implement the four
 * hardware-facing virtuals.
 *
 * ## Architectural Role
 * IngressBase is the boundary between the kernel ring (or XDP socket) and the
 * protocol stack. It does not parse packets; it only delivers raw @ref FrameView
 * objects upward and manages slot lifetimes downward.
 *
 * ## Lifecycle & Ownership
 * Constructed by @ref hardware::ingress::create(). Owned by @ref RxQueueManager
 * via a @c QueueState entry. @ref start() launches the RX thread; @ref stop()
 * signals the thread and joins it before the destructor runs.
 *
 * ## Concurrency Model
 * The run loop is strictly single-threaded. @ref releaseFrame() and
 * @ref flushReturns() are called only from the ingress thread. @ref start()
 * and @ref stop() are called from the manager thread and are synchronized via
 * the @c running atomic.
 *
 * ## Fast Path vs. Slow Path
 * - Fast path: @ref pollFrame() → process payload → @ref releaseFrame() per frame.
 * - Slow path: @ref waitEvent() (blocks when no frames are available).
 *
 * @see IngressXdp, IngressPacket, RxQueueManager
 */
class IngressBase
{
public:
    /**
     * @brief Constructs IngressBase with the given interface and queue options.
     *
     * Initialises the return batch buffer. The RX thread is NOT started here;
     * call @ref start() explicitly after construction.
     *
     * @param iface  Interface to receive on.
     * @param opts   Queue configuration (ifname, frame count, CPU affinity, fanout…).
     */
    IngressBase(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts);

    /**
     * @brief Destroys IngressBase.
     *
     * @warning @ref stop() must be called and must have returned before the
     * destructor runs. Destroying a running IngressBase is a data race.
     */
    virtual ~IngressBase();

    /**
     * @brief Launches the RX thread and begins receiving frames.
     *
     * Sets @c running to @c true and starts @ref runLoop() on @c ingressThread.
     * Has no effect if already started.
     */
    void start();

    /**
     * @brief Signals the RX thread to stop and blocks until it exits.
     *
     * Sets @c running to @c false, calls @ref stopRx() to unblock any pending
     * @ref waitEvent(), then joins @c ingressThread. After this returns it is
     * safe to destroy the object.
     */
    void stop();

    qos::ingress::RxQueueOpts opts;

protected:
    // SUBCLASS INTERFACE

    /**
     * @brief Dequeues one received frame from the hardware ring or socket.
     *
     * Non-blocking. Returns @c false immediately if no frame is ready; returns
     * @c true and populates @p out when a frame is available. The caller must
     * eventually call @ref releaseFrame() with @c out.index.
     *
     * @param[out] out  Populated with payload pointer, length, and slot index.
     * @return @c true if a frame was dequeued; @c false if the ring is empty.
     */
    virtual bool pollFrame(FrameView& out) = 0;

    /**
     * @brief Blocks until at least one frame is available or @ref stopRx() is called.
     *
     * Typically implemented with @c epoll_wait on the socket file descriptor.
     * Must return promptly after @ref stopRx() writes to an eventfd.
     */
    virtual void waitEvent() = 0;

    /**
     * @brief Returns the frame at @p index to the kernel ring.
     *
     * Called in batches from @ref flushReturns(). Must not block.
     * @param index  Slot index from a @ref FrameView previously returned by @ref pollFrame().
     */
    virtual void returnToDevice(uint32_t index) = 0;

    /**
     * @brief Blocks until all outstanding frames have been released by the consumer.
     *
     * Used during @ref stop() to drain in-flight frames before teardown.
     * Default is a no-op; subclasses that track outstanding counts override this.
     */
    virtual void waitUntilAllFramesReleased() {}

    /**
     * @brief Signals the hardware to stop delivering new frames.
     *
     * Called from the manager thread via @ref stop(). Must unblock any
     * concurrent @ref waitEvent() call on the ingress thread.
     */
    virtual void stopRx() = 0;

    /**
     * @brief Hook called just before @ref flushReturns() invokes @ref returnToDevice().
     *
     * AF_XDP overrides this to commit deferred fill-ring writes before the
     * batch of returned frames is handed back, ensuring the kernel sees freed
     * slots before new frames are requested.
     */
    virtual void onReturnFlush() {}

    // HELPERS FOR SUBCLASSES

    /**
     * @brief Buffers @p index for return; flushes the batch when full.
     *
     * Accumulates frame indices and calls @ref flushReturns() automatically
     * when the internal buffer (@c RETURN_BATCH entries) is full. This amortises
     * @ref returnToDevice() kernel calls across a batch.
     *
     * @param index  Frame index to queue for return to the device.
     */
    void releaseFrame(uint32_t index);

    /**
     * @brief Immediately returns all buffered frame indices to the device.
     *
     * Calls @ref onReturnFlush() then @ref returnToDevice() for each buffered
     * index. Resets the buffer count to zero.
     */
    void flushReturns();

    /**
     * @brief Main receive loop: poll → process → release, until @c running is false.
     *
     * Called on @c ingressThread. Falls through to @ref waitEvent() when
     * @ref pollFrame() returns @c false to avoid busy-spinning.
     */
    void runLoop();

protected:
    interface::Interface& iface; ///< Interface this queue receives on.
    const uint32_t        qid;   ///< Hardware queue index assigned at construction.

    std::atomic<bool> running{false}; ///< Set to false by stop() to terminate runLoop().
    std::thread       ingressThread;  ///< The dedicated RX thread.

private:
    /**
     * @brief Small per-thread batch — avoids per-frame returnToDevice() overhead.
     *
     * 64 entries is enough to amortise the cost without adding latency.
     */
    static constexpr uint32_t RETURN_BATCH = 64;
    uint32_t returnBuf[RETURN_BATCH]; ///< Pending frame indices awaiting return.
    uint32_t returnCount = 0;         ///< Number of valid entries in returnBuf.
};

} // namespace hardware::ingress

#endif // INGRESS_BASE_H
