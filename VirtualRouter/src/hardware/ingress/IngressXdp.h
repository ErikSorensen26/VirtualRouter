// IngressXdp.h

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
    bool pollFrame(FrameView& out)          override;
    void waitEvent()                        override;
    void returnToDevice(uint32_t index)     override;
    void onReturnFlush()                    override;
    void stopRx()                           override;
    void waitUntilAllFramesReleased()       override;

private:
    // SETUP
    void setupSocket();
    void setupUmem();
    void configureRings();
    void mmapRings();
    void bindSocket();
    void prefillFillRing();
    void checkWakeSupport();
    void setupEvents();
    void teardownEvents();

    void     kickIfNeeded();
    uint32_t refillRxCache();

    uint64_t frameAddr(uint32_t idx) const noexcept
    {
        return uint64_t(idx) * uint64_t(frameSize);
    }

    // CONFIG
    uint32_t frameCount = 0; ///< UMEM frame count (power-of-2).
    uint32_t frameMask  = 0; ///< frameCount - 1, for fast modulo.
    uint32_t frameSize  = 0; ///< Bytes per UMEM chunk (always 4096).
    uint64_t umemSize   = 0; ///< Total UMEM allocation size.

    // SOCKET
    int  xskFd      = -1;
    int  epfd       = -1;
    int  evtfd      = -1;   ///< Written by stopRx() to unblock epoll_wait.
    bool zeroCopy   = false;
    bool needWakeup = false; ///< True when XDP_RING_NEED_WAKEUP is active.

    // UMEM
    void* umemArea = nullptr;

    struct xdp_mmap_offsets off{};

    // RX RING
    uint32_t         rxEntries     = 0;
    uint32_t         rxMask        = 0;
    void*            rxRingArea    = nullptr;
    size_t           rxRingMapSize = 0;
    uint32_t*        rxProducer    = nullptr;
    uint32_t*        rxConsumer    = nullptr;
    struct xdp_desc* rxDesc        = nullptr;
    uint32_t         rxConsShadow  = 0;

    // FILL RING
    uint32_t  fqEntries    = 0;
    uint32_t  fqMask       = 0;
    void*     fqArea       = nullptr;
    size_t    fqMapSize    = 0;
    uint32_t* fqProducer   = nullptr;
    uint32_t* fqConsumer   = nullptr;
    uint64_t* fqAddr       = nullptr;
    uint32_t  fqProdShadow = 0; ///< Local shadow; committed in onReturnFlush().

    // RX BATCH CACHE
    static constexpr uint32_t RX_CACHE_CAP = 512;

    struct RxItem { uint32_t idx; uint32_t len; };
    RxItem   rxCache[RX_CACHE_CAP];
    uint32_t rxHead = 0;
    uint32_t rxTail = 0;

    std::atomic<uint32_t> outstanding{0}; ///< Frames currently in user-space.
};

} // namespace hardware::ingress

#endif // INGRESS_XDP_H
