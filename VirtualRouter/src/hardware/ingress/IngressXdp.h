// IngressXdp.h

#ifndef INGRESS_XDP_H
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
    void releaseFrame(uint32_t index) override;
    void waitForData() override;
    void stopRx() override;
    void waitUntilAllFramesReleased() override;

private:
    void setupSocket();
    void setupUmem();
    void configureRings();
    void mmapRings();
    void bindSocket();
    void prefillFillRing();
    void checkWakeSupport();

    inline uint64_t frameAddr(uint32_t idx) const
    {
        return uint64_t(idx) * uint64_t(frameSize);
    }
    inline void kickIfNeeded();

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

    // UMEM
    void* umemArea = nullptr;

    // Offsets (value, not pointer)
    struct xdp_mmap_offsets off{};

    // RX ring
    uint32_t rxEntries = 0;
    void* rxRingArea = nullptr;
    size_t rxRingMapSize = 0;
    uint32_t* rxRingProducer = nullptr;
    uint32_t* rxRingConsumer = nullptr;
    struct xdp_desc* rxRingDesc = nullptr;

    // Fill ring
    uint32_t fqEntries = 0;
    void* fqArea = nullptr;
    size_t fqMapSize = 0;
    uint32_t* fqProducer = nullptr;
    uint32_t* fqConsumer = nullptr;
    uint64_t* fqDesc = nullptr;

    std::atomic_bool* frameInUse = nullptr;
};

#endif
