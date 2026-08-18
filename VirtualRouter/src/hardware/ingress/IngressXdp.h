/**
 * @file IngressXdp.h
 * @brief AF_XDP (XSK) zero-copy packet ingress backend.
 * @ingroup HARDWARE_INGRESS
 *
 * Implements kernel-bypass packet reception using Linux AF_XDP (XDP Socket).
 * Packets are DMA'd directly into a userspace UMEM region shared with the kernel,
 * eliminating per-packet copies in zero-copy mode. Uses epoll for event-driven
 * packet availability detection.
 */

#ifndef INGRESS_XDP_H
#define INGRESS_XDP_H

#include "IngressBase.h"
#include <linux/if_xdp.h>
#include <sys/epoll.h>
#include <atomic>

namespace hardware::ingress
{

/**
 * @class IngressXdp
 * @brief AF_XDP (XSK) ingress backend.
 *
 * Receives frames via an XDP socket bound to a pre-loaded XDP program on the
 * interface.  Frames are placed directly into a UMEM region shared with the
 * kernel, eliminating per-packet copies in zero-copy mode.
 *
 * Falls back gracefully in the factory if AF_XDP is not supported or no XDP
 * program is loaded on the target interface.
 *
 * ## Ring model
 * - **RX ring**: kernel → user.  Each descriptor holds a UMEM offset + length.
 * - **Fill ring**: user → kernel.  We replenish it with free frame offsets so
 *   the kernel has somewhere to write incoming packets.
 *
 * ## Thread model
 * Single consumer thread (IngressBase::runLoop).  `returnToDevice` and
 * `pollFrame` are called only from that thread; no locking needed.
 */
class IngressXdp : public IngressBase
{
public:

    /**
     * @brief Construct an XDP ingress queue.
     *
     * Allocates and locks the UMEM region, creates the XDP socket, configures
     * RX and fill rings, binds to the interface queue, and pre-fills the fill
     * ring so the kernel can start delivering packets immediately.
     *
     * @param iface  Interface this queue receives on.
     * @param opts   Queue options (ifname, frameCount, snapLen, cpuId, fanout…).
     * @throws std::runtime_error on any setup failure.
     */
    IngressXdp(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts);
    ~IngressXdp() override;

protected:
    /// @brief Polls the RX ring for a received frame; returns true if frame available.
    bool pollFrame(FrameView& out)          override;
    
    /// @brief Blocks until a frame is available (epoll_wait on wake-up event).
    void waitEvent()                        override;
    
    /// @brief Returns a frame back to the kernel (adds offset to fill ring).
    void returnToDevice(uint32_t index)     override;
    
    /// @brief Commits batched frame returns to fill ring (updates producer pointer).
    void onReturnFlush()                    override;
    
    /// @brief Stops packet reception (closes RX ring, signals epoll).
    void stopRx()                           override;
    
    /// @brief Blocks until all in-flight frames have been returned to kernel.
    void waitUntilAllFramesReleased()       override;

private:
    // === SETUP ===
    /// @brief Creates and configures the AF_XDP socket.
    void setupSocket();
    
    /// @brief Allocates, locks, and registers UMEM region with kernel.
    void setupUmem();
    
    /// @brief Configures RX ring and fill ring sizes/modes.
    void configureRings();
    
    /// @brief Memory-maps kernel ring structures into userspace.
    void mmapRings();
    
    /// @brief Binds XDP socket to interface queue.
    void bindSocket();
    
    /// @brief Pre-fills fill ring with available frame offsets for kernel.
    void prefillFillRing();
    
    /// @brief Queries and enables XDP_RING_NEED_WAKEUP if supported.
    void checkWakeSupport();
    
    /// @brief Sets up epoll and eventfd for packet wake-up events.
    void setupEvents();
    
    /// @brief Cleans up epoll and eventfd.
    void teardownEvents();

    /// @brief Kicks the kernel if XDP_RING_NEED_WAKEUP flag is set.
    void     kickIfNeeded();
    
    /// @brief Refills RX cache from the RX ring; returns count of frames added.
    uint32_t refillRxCache();

    /// @brief Computes the kernel UMEM address for a frame index.
    uint64_t frameAddr(uint32_t idx) const noexcept
    {
        return uint64_t(idx) * uint64_t(frameSize);
    }

    // === CONFIGURATION ===
    uint32_t frameCount = 0; ///< UMEM frame count (power-of-2).
    uint32_t frameMask  = 0; ///< frameCount - 1, for fast modulo arithmetic.
    uint32_t frameSize  = 0; ///< Bytes per UMEM chunk (always 4096).
    uint64_t umemSize   = 0; ///< Total UMEM allocation size in bytes.

    // === SOCKET STATE ===
    int  xskFd      = -1;      ///< AF_XDP socket file descriptor.
    int  epfd       = -1;      ///< epoll file descriptor for event detection.
    int  evtfd      = -1;      ///< Eventfd written by stopRx() to unblock epoll_wait.
    bool zeroCopy   = false;   ///< True if kernel supports zero-copy mode.
    bool needWakeup = false;   ///< True when XDP_RING_NEED_WAKEUP flag is active.

    // === UMEM REGION ===
    void* umemArea = nullptr;  ///< Mmap'd UMEM region containing frame buffers.
    
    /// Kernel-provided UMEM ring offsets (for mmap calculations).
    struct xdp_mmap_offsets off{};

    // === RX RING (kernel → user) ===
    uint32_t         rxEntries     = 0;     ///< RX ring capacity (power-of-2).
    uint32_t         rxMask        = 0;     ///< rxEntries - 1, for fast modulo.
    void*            rxRingArea    = nullptr; ///< Mmap'd RX ring region.
    size_t           rxRingMapSize = 0;     ///< Size of RX ring mmap region.
    uint32_t*        rxProducer    = nullptr; ///< Kernel's RX producer pointer.
    uint32_t*        rxConsumer    = nullptr; ///< Our RX consumer pointer.
    struct xdp_desc* rxDesc        = nullptr; ///< RX descriptor array (offset, length pairs).
    uint32_t         rxConsShadow  = 0;     ///< Local shadow of consumer; used for batching.

    // === FILL RING (user → kernel) ===
    uint32_t  fqEntries    = 0;     ///< Fill ring capacity (power-of-2).
    uint32_t  fqMask       = 0;     ///< fqEntries - 1, for fast modulo.
    void*     fqArea       = nullptr; ///< Mmap'd fill ring region.
    size_t    fqMapSize    = 0;     ///< Size of fill ring mmap region.
    uint32_t* fqProducer   = nullptr; ///< Our fill ring producer pointer.
    uint32_t* fqConsumer   = nullptr; ///< Kernel's fill ring consumer pointer.
    uint64_t* fqAddr       = nullptr; ///< Fill ring address array (UMEM offsets).
    uint32_t  fqProdShadow = 0;     ///< Local shadow; committed in onReturnFlush().

    // === RX BATCH CACHE ===
    /// Maximum frames cached from RX ring before polling again.
    static constexpr uint32_t RX_CACHE_CAP = 512;

    /// @brief Cached RX descriptor (frame index and length).
    struct RxItem { uint32_t idx; uint32_t len; };
    
    /// Circular buffer of cached RX frames ready for processing.
    RxItem   rxCache[RX_CACHE_CAP];
    
    /// Head pointer (insertion point) in rxCache.
    uint32_t rxHead = 0;
    
    /// Tail pointer (extraction point) in rxCache.
    uint32_t rxTail = 0;

    /// Frames currently in user-space (not yet returned to kernel).
    std::atomic<uint32_t> outstanding{0};
};

} // namespace hardware::ingress

#endif // INGRESS_XDP_H
