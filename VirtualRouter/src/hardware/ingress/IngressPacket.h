// IngressPacket.h

#ifndef INGRESS_PACKET_H
#define INGRESS_PACKET_H

#include "IngressBase.h"
#include <linux/if_packet.h>
#include <linux/if_ether.h>

class IngressPacket : public IngressBase
{
public:
    IngressPacket(const char* ifname, Interface& iface, uint32_t qid,
                  uint32_t frameCount, uint32_t snapLen);
    ~IngressPacket() override;

protected:
    bool pollFrame(FrameView& out) override;
    void releaseFrame(uint32_t index) override;
    void waitForData() override;
    void stopRx() override;
    void waitUntilAllFramesReleased() override;

private:
    void setupSocket();
    void setupRing();
    void mmapRing();
    void bindIface();
    inline uint8_t* framePtr(uint32_t idx) const
    {
        return reinterpret_cast<uint8_t*>(ring) + sizeof(idx) * frameSizeRing;
    }

private:

    const uint32_t ringFrames;
    const uint32_t snapLen;
    uint32_t frameSizeRing;

    int fd = -1;
    void* ring = nullptr;
    size_t ringLen = 0;

    uint32_t nextIdx = 0;

    std::atomic_bool* frameInUse = nullptr;
};

#endif
