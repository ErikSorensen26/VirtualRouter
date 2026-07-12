/**
 * @file NullEgress.h
 * @brief In-memory egress backend with no NIC or socket, for interfaces with no real hardware
 * and for testing.
 */

#ifndef NULL_EGRESS_H
#define NULL_EGRESS_H

#include "EgressBase.h"

namespace hardware::egress
{

/**
 * @brief Egress backend that allocates real frame memory but never touches a NIC or socket.
 * @ingroup HARDWARE_EGRESS
 *
 * `NullEgress` implements the same @ref EgressBase contract as @ref EgressPacket and
 * @ref EgressSend, but @ref send() is a no-op that immediately reclaims the frame. It exists
 * for interfaces that have no backing hardware (test doubles): @ref hardware::egress::create()
 * requires a real socket, which fails without `CAP_NET_RAW`. `NullEgress` gives callers a
 * working frame-allocation path (so @ref processing::PacketBuilder construction and
 * @ref qos::egress::TxDistributor::send() succeed) without requiring any privilege or NIC.
 *
 * ## Architectural Role
 * Not selected by @ref hardware::egress::create(); constructed directly by
 * @ref interface::Interface when hardware integration is disabled for that interface.
 *
 * ## Lifecycle & Ownership
 * Allocates the frame buffer in the constructor; frees it in the destructor. No socket,
 * no kernel ring, no OS resource is ever acquired.
 *
 * @see EgressBase, EgressPacket, EgressSend
 */
class NullEgress : public EgressBase
{
public:
    /**
     * @brief Allocates frame memory for @param opts.frameCount frames (or a small default).
     *
     * @param iface Interface this egress queue is attached to.
     * @param opts  Queue configuration (frame count, snap length).
     */
    explicit NullEgress(interface::Interface& iface, const qos::egress::TxQueueOpts& opts);

    /**
     * @brief Frees the frame buffer.
     */
    ~NullEgress() override;

    /**
     * @brief No-op: immediately returns the frame to the free ring without transmitting.
     */
    bool send(uint32_t index, uint32_t length) noexcept override;

    /**
     * @brief No-op: nothing to reclaim without a kernel ring.
     */
    void reclaim() override {}

    /**
     * @brief Returns frame @param index to the free ring without transmitting.
     */
    void cancel(uint32_t index) override;

private:
    void mapFrame(uint32_t index, FrameHandle& out) override;

private:
    uint8_t* frameArea   = nullptr; ///< Heap-allocated buffer holding all frame slots.
    uint32_t frameCount  = 0;       ///< Number of frame slots in @c frameArea.
    size_t   frameStride = 0;       ///< Bytes per frame slot (payload + PacketSlot + alignment).
};
} // namespace hardware::egress

#endif // NULL_EGRESS_H
