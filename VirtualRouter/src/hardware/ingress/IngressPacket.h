/**
 * @file IngressPacket.h
 * @brief Raw PACKET_MMAP socket ingress backend for Linux kernel packet capture.
 */

#ifndef INGRESS_PACKET_H
#define INGRESS_PACKET_H

#include "IngressBase.h"
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <sys/epoll.h>
#include <atomic>

namespace hardware::ingress
{

/**
 * @brief TPACKET_V2 mmap-based packet socket ingress backend.
 * @ingroup HARDWARE_INGRESS
 *
 * Implements zero-copy packet reception using Linux kernel TPACKET_V2 with
 * mmap ring buffers. Each frame is delivered by the kernel into pre-allocated
 * blocks, with block-level metadata tracking in-flight frames.
 *
 * ## Architectural Role
 * Sits between the kernel's PACKET_MMAP socket and IngressBase. Handles all
 * kernel-specific setup (socket creation, ring mmap, epoll registration) and
 * implements pollFrame() to extract frames from ring blocks.
 *
 * ## Lifecycle & Ownership
 * Constructed with an Interface reference and RxQueueOpts. Binds to the kernel
 * socket, mmaps the ring, and registers with epoll. Destructor unmaps the ring
 * and tears down the socket.
 *
 * ## Concurrency Model
 * All operations are called from a single receiver thread (owned by IngressBase).
 * The atomic blkInFlight counter coordinates in-flight block tracking.
 *
 * @warning The ring buffer must remain mmap'd for the kernel to deliver packets.
 * Calling waitEvent() without prior pollFrame() or returning frames that were
 * never polled corrupts the ring state.
 *
 * @see IngressBase, IngressXdp
 */
class IngressPacket : public IngressBase
{
public:
    /**
     * @brief Constructs a TPACKET_V2 ingress instance and binds to the kernel socket.
     *
     * Performs complete socket setup: creates PACKET_MMAP socket, binds to interface,
     * configures TPACKET_V2, allocates and mmaps the ring, and registers with epoll.
     *
     * @param iface        Reference to the interface to capture from.
     * @param opts         RX queue configuration (frame count, block size, retire time, etc.).
     *
     * @warning Throws std::runtime_error if socket setup, mmap, or epoll registration fails.
     */
    IngressPacket(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts);

    /**
     * @brief Destructs the TPACKET_V2 ingress and releases kernel resources.
     *
     * Unmaps the ring buffer, closes the socket, and cleans up epoll registration.
     * Must be called only after stopRx() has been invoked by IngressBase.
     */
    ~IngressPacket();

protected:
    /**
     * @brief Polls the next available frame from the ring.
     *
     * Checks the current block for filled frames marked by the kernel. Returns
     * the frame metadata (buffer pointer, length, ring index) for the caller to
     * process. Advances the frame index and moves to the next block when current
     * block is exhausted.
     *
     * @param[out] out Frame metadata to populate.
     * @return True if a frame was available, false if no frames ready.
     */
    bool pollFrame(FrameView& out) override;

    /**
     * @brief Waits for the kernel to deliver packets into the ring.
     *
     * Blocks on epoll until the kernel signals data availability or timeout.
     * Used by IngressBase to coordinate the receiver thread poll loop.
     */
    void waitEvent() override;

    /**
     * @brief Returns a processed frame's ring index to the kernel.
     *
     * Marks the frame slot as consumed so the kernel can reuse it.
     * Decrements the in-flight counter for the frame's block.
     *
     * @param index Ring index returned by pollFrame().
     */
    void returnToDevice(uint32_t index) override;

    /**
     * @brief Stops packet reception and begins cleanup.
     *
     * Signals the kernel to stop delivering packets and closes the epoll registration.
     */
    void stopRx() override;

    /**
     * @brief Waits until all in-flight frames have been returned to the kernel.
     *
     * Polls blkInFlight counters until all blocks have zero frames in-flight.
     * Used during shutdown to ensure no frames are lost or leaked.
     */
    void waitUntilAllFramesReleased() override;

private:
    void setupEvents();    ///< Create and configure the epoll file descriptor.
    void bindIface();      ///< Bind the socket to the configured interface.
    void setupRing();      ///< Configure TPACKET_V2 and allocate ring buffer.
    void mmapRing();       ///< Map the ring buffer into address space.
    void setupSocket();    ///< Create the PACKET_MMAP socket and set socket options.
    void teardownEvents(); ///< Clean up epoll registration.

    int fd = -1;                      ///< Socket file descriptor.
    int epfd = -1;                    ///< Epoll file descriptor for event notification.
    int evtfd = -1;                   ///< Eventfd written by stopRx() to unblock epoll_wait.
    void* ring = nullptr;             ///< Mmap'd ring buffer base address.
    size_t ringLen = 0;               ///< Total ring buffer size in bytes.

    uint32_t frameSizeRing = 0;       ///< Frame size as configured for the ring.
    uint32_t blockSize = 0;           ///< Size of each block in the ring.
    uint32_t framesPerBlock = 0;      ///< Number of frame slots in each block.
    uint32_t blockNr = 0;             ///< Current block index being polled.
    uint32_t blockMask = 0;           ///< Bitmask for wrapping block index.
    uint32_t nextIdx = 0;             ///< Next frame index within current block.

    uint32_t* blockNextOff = nullptr; ///< Per-block offset to next frame (from kernel).
    uint16_t* blockRemain = nullptr;  ///< Per-block count of frames awaiting return.

    std::atomic<uint32_t>* blkInFlight = nullptr; ///< Per-block in-flight frame counter.
};

} // namespace hardware::ingress

#endif

