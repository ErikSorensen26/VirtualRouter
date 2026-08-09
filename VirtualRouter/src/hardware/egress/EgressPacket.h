/**
 * @file EgressPacket.h
 * @brief TPACKET_V2 memory-mapped TX ring egress backend.
 */

#ifndef EGRESS_PACKET_H
#define EGRESS_PACKET_H

#include "EgressBase.h"
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <sys/epoll.h>
#include <atomic>

namespace hardware::egress
{

/**
 * @brief TPACKET_V2 memory-mapped AF_PACKET TX ring egress backend.
 * @ingroup HARDWARE_EGRESS
 *
 * Maps a kernel TX ring directly into process address space. Frames are written
 * in place and submitted to the kernel with a single @c sendto() kick per
 * batch, avoiding per-packet syscall overhead.
 *
 * ## Architectural Role
 * Primary egress backend when @c PACKET_MMAP is available. Selected by
 * @ref hardware::egress::create(). Falls back to @ref EgressSend if the
 * socket setup fails.
 *
 * ## Lifecycle & Ownership
 * Allocates the AF_PACKET socket, configures @c TPACKET_V2, maps the TX ring,
 * and calls @ref initFreeRing() during construction. The destructor unmaps the
 * ring and closes the socket.
 *
 * ## Concurrency Model
 * - @ref send(), @ref reclaim(), @ref cancel(), @ref flush(), @ref waitWritable()
 *   — called only from the single @ref BaseQueue consumer thread.
 * - @ref mapFrame() — called from @ref getFrame() which is MPMC-safe
 *   (inherited from @ref EgressBase).
 * - @c pendingKicks — @c std::atomic; incremented on each @ref send() call and
 *   tested in @ref flush() / @ref kickKernelCached() to batch syscalls.
 *
 * ## Fast Path vs. Slow Path
 * - Fast path: @ref send() marks the slot @c TP_STATUS_SEND_REQUEST; no syscall.
 * - Slow path: @ref flush() issues one @c sendto() to kick all pending frames.
 *   @ref reclaim() polls for @c TP_STATUS_AVAILABLE to return slots to the free ring.
 *
 * @see EgressBase, EgressSend
 */
class EgressPacket : public EgressBase
{
    std::atomic<uint32_t> pendingKicks = 0; ///< Frames awaiting a kernel kick; reset in flush().

public:

    /**
     * @brief Constructs the TPACKET_V2 backend: opens socket, maps ring, initialises free ring.
     *
     * @param iface  Interface to transmit on.
     * @throws std::runtime_error if any kernel setup step fails.
     */
    EgressPacket(interface::Interface& iface, const qos::egress::TxQueueOpts&);

    /**
     * @brief Unmaps the TX ring, tears down the epoll instance, and closes the socket.
     */
    ~EgressPacket() override;

    /**
     * @brief Marks frame @p index as @c TP_STATUS_SEND_REQUEST without issuing a syscall.
     *
     * Increments @c pendingKicks. The kernel is notified lazily by @ref flush().
     *
     * @param index   Frame index (as returned by @ref EgressBase::getFrame()).
     * @param length  Payload length in bytes.
     * @return Always @c true; failures are reported via @ref reclaim() instead.
     */
    bool send(uint32_t index, uint32_t length) noexcept override;

    /**
     * @brief Scans the TX ring for @c TP_STATUS_AVAILABLE frames and returns them to the free ring.
     *
     * Should be called periodically by the consumer thread to prevent the
     * free ring from draining.
     */
    void reclaim() override;

    /**
     * @brief Returns frame @p index to the free ring without transmitting it.
     * @param index  Frame index obtained from @ref EgressBase::getFrame().
     */
    void cancel(uint32_t index) override;

    /**
     * @brief Issues a @c sendto() kick if any frames are pending.
     *
     * Resets @c pendingKicks to zero after the syscall. Called at end-of-batch
     * by @ref BaseQueue to avoid stranded frames.
     */
    void flush() override;

    /**
     * @brief Blocks via @c epoll_wait until the TX ring has at least one writable slot.
     *
     * Used for back-pressure when the ring is full. Returns immediately if a
     * slot is already available.
     */
    void waitWritable() override;

private:
    void mapFrame(uint32_t index, FrameHandle& out) override;
    void onAllocNudge() override;

private:
    int fd = -1;
    int epfd = -1;
    void* ring = nullptr;
    size_t ringLen = 0;

    struct tpacket_req req{};
    uint32_t frameCount = 0;

    uint32_t reclaimCursor = 0;
    uint32_t maxPayload;
    uint8_t* frameBase;

private:
    void setupSocket();
    void bindIface();
    void setupRing();
    void mmapRing();
    void setupEvents();
    void teardownEvents();

    void kickKernelCached();

    void reclaimImpl(uint32_t limit);
};

} // namespace hardware

#endif

