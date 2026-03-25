// IngressPacket.h

#ifndef INGRESS_PACKET_H
#define INGRESS_PACKET_H

#include "IngressBase.h"
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <sys/epoll.h>
#include <atomic>

namespace hardware::ingress
{

class IngressPacket : public IngressBase
{
public:
    IngressPacket(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts);
    ~IngressPacket();

protected:
    bool pollFrame(FrameView& out) override;
    void waitEvent() override;
    void returnToDevice(uint32_t index) override;
    void stopRx() override;
    void waitUntilAllFramesReleased() override;

private:
    void setupEvents();
    void bindIface();
    void setupRing();
    void mmapRing();
    void setupSocket();
    void teardownEvents();

    int fd = -1, epfd = -1;
    void* ring = nullptr;
    size_t ringLen = 0;

    uint32_t frameSizeRing = 0;
    uint32_t blockSize = 0;
    uint32_t framesPerBlock = 0;
    uint32_t blockNr = 0;
    uint32_t blockMask = 0;
    uint32_t nextIdx = 0;

    uint32_t* blockNextOff = nullptr;
    uint16_t* blockRemain = nullptr;

    std::atomic<uint32_t>* blkInFlight = nullptr;
};

} // namespace hardware

#endif

