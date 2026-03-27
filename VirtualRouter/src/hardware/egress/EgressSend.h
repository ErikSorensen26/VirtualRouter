/**
 * @file EgressSend.h
 * @brief Fallback egress backend using a plain AF_PACKET sendto() per frame.
 */

#ifndef EGRESS_SEND_H
#define EGRESS_SEND_H

#include <linux/if_packet.h>

#include "EgressBase.h"

namespace hardware::egress
{

/**
 * @brief Fallback egress backend that transmits each frame with a single @c AF_PACKET @c sendto().
 * @ingroup HARDWARE_EGRESS
 *
 * `EgressSend` is used when TPACKET_V2 mmap ring support is unavailable. Each call to
 * @ref send() results in one @c sendto() system call, which has higher per-packet overhead
 * than @ref EgressPacket but requires no special kernel support beyond @c AF_PACKET.
 *
 * Frame memory is heap-allocated at construction time into a flat buffer; @ref mapFrame()
 * returns a pointer into that buffer for each frame index.
 *
 * ## Architectural Role
 * Selected by @ref hardware::egress::create() when TPACKET_V2 ring setup fails.
 * Implements the same @ref EgressBase interface as @ref EgressPacket so callers are
 * unaware of the fallback.
 *
 * ## Lifecycle & Ownership
 * - Opens an @c AF_PACKET socket and allocates the frame buffer in the constructor.
 * - Closes the socket and frees the buffer in the destructor.
 *
 * ## Concurrency Model
 * Inherits the @ref EgressBase concurrency contract: @ref send(), @ref cancel(),
 * @ref reclaim(), @ref flush(), and @ref waitWritable() are called from the single
 * @ref BaseQueue consumer thread only.
 *
 * ## Fast Path vs. Slow Path
 * - Fast path: @ref send() — one @c sendto() per frame; no batching.
 * - @ref reclaim() and @ref flush() are no-ops (nothing to reclaim or batch).
 *
 * @see EgressBase, EgressPacket
 */
class EgressSend : public EgressBase
{
public:
    /**
     * @brief Opens the @c AF_PACKET socket, binds it to @p iface, and allocates the frame buffer.
     *
     * @param iface  Interface to transmit on.
     * @param opts   Queue options (frame count, snap length, CPU affinity, etc.).
     * @throws std::runtime_error if socket creation, binding, or buffer allocation fails.
     */
    explicit EgressSend(interface::Interface& iface, const qos::egress::TxQueueOpts& opts);

    /**
     * @brief Frees the frame buffer and closes the @c AF_PACKET socket.
     */
    ~EgressSend() override;

    /**
     * @brief Transmits frame @p index via @c sendto() on the @c AF_PACKET socket.
     *
     * @param index   Frame index allocated by @ref EgressBase::getFrame().
     * @param length  Number of valid payload bytes to send.
     * @return @c true if @c sendto() succeeded; @c false on a syscall error.
     */
    bool send(uint32_t index, uint32_t length) noexcept override;

    /** @brief No-op: @c AF_PACKET @c sendto() does not buffer frames in a kernel ring. */
    void reclaim() override {}

    /**
     * @brief Returns frame @p index to the free ring without transmitting it.
     * @param index  Frame index obtained from @ref EgressBase::getFrame().
     */
    void cancel(uint32_t index) override;

    /** @brief No-op: no batching is performed by this backend. */
    void flush() override {}

    /** @brief No-op: @c sendto() blocks the caller until the kernel accepts the frame. */
    void waitWritable() override {}

private:
    void mapFrame(uint32_t index, FrameHandle& out) override;

private:
    int      sockFd      = -1;      ///< AF_PACKET socket file descriptor.
    uint8_t* frameArea   = nullptr; ///< Heap-allocated buffer holding all frame slots.
    uint32_t frameCount  = 0;       ///< Number of frame slots in @c frameArea.
    size_t   frameStride = 0;       ///< Bytes per frame slot (payload + PacketSlot + alignment).
    sockaddr_ll addr{};             ///< Pre-built destination address used in every @c sendto().
};

} // namespace hardware

#endif // EGRESS_SEND_H

