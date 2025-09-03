// IngressXdp.h

/*#ifndef INGRESS_XDP_H
#define INGRESS_XDP_H

#include "IngressBase.h"
#include <linux/if_xdp.h>

class IngressXdp : public IngressBase
{
public:
    IngressXdp(const char* ifname, Interface& iface, uint32_t qid,
               uint32_t frameCount, uint32_t frameSize);
    ~IngressXdp() override;

protected:
    bool pollFrame(FrameView& out) override;
    void stopRx() override;
    void waitUntilAllFramesReleased() override;
    void waitEvent() override;
    void signalStop() override;
    void returnToDevice(uint32_t index) override;
    void onReturnNudge() override;

private:
    void setupSocket();
    void setupUmem();
    void configureRings();
    void mmapRings();
    void bindSocket();
    void prefillFillRing();
    void checkWakeSupport();
    void setupEvents();
    void teardownEvents();

    inline uint64_t frameAddr(uint32_t idx) const { return uint64_t(idx) * uint64_t(frameSize); }
    inline void kickIfNeeded();

    inline uint32_t refillRxCache();

private:
    // config
    const uint32_t frameCount;
    const uint32_t frameMask;
    const uint32_t frameSize;
    const uint64_t umemSize;

    // socket + mode
    int xskFd = -1;
    bool zeroCopy = false;
    bool needWakeup = false;

    int epfd = -1;
    int evtfd = -1;

    // UMEM
    void* umemArea = nullptr;

    // Offsets (value, not pointer)
    struct xdp_mmap_offsets off{};

    // RX ring
    uint32_t rxEntries = 0;
    uint32_t rxMask = 0;
    void* rxRingArea = nullptr;
    size_t rxRingMapSize = 0;
    uint32_t* rxProducer = nullptr;
    uint32_t* rxConsumer = nullptr;
    struct xdp_desc* rxDesc = nullptr;

    // Local shadows
    uint32_t rxProdCached = 0;
    uint32_t rxConsShadow = 0;

    // Fill ring
    uint32_t fqEntries = 0;
    uint32_t fqMask = 0;
    void* fqArea = nullptr;
    size_t fqMapSize = 0;
    uint32_t* fqProducer = nullptr;
    uint32_t* fqConsumer = nullptr;
    uint64_t* fqAddr = nullptr;

    // Local producer shadow for batching
    uint32_t fqProdShadow = 0;

    static constexpr uint32_t RX_CACHE_CAP = 512;
    struct RxItem { uint32_t idx; uint32_t len; };
    RxItem rxCache[RX_CACHE_CAP];
    uint32_t rxHead = 0;
    uint32_t rxTail = 0;

    static constexpr uint32_t FQ_COMMIT_EVERY = 64;
    uint32_t fqSinceCommit = 0;

    std::atomic<uint32_t> outstanding = 0;
    std::atomic<bool> stopping = false;
};

#endif // INGRESS_XDP_H*/
