// EgressPacket.h

#ifndef EGRESS_PACKET_H
#define EGRESS_PACKET_H

#include "EgressBase.h"
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <sys/epoll.h>
#include <atomic>

namespace hardware::egress
{

class EgressPacket : public EgressBase
{
    std::atomic<uint32_t> pendingKicks = 0;

public:

    EgressPacket(interface::Interface& iface, const qos::egress::TxQueueOpts&);
    ~EgressPacket() override;

    bool send(uint32_t index, uint32_t length) noexcept override;
    void reclaim() override;
    void cancel(uint32_t index) override;
    void flush() override;
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

