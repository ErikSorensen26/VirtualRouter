// EgressSend.h

#ifndef EGRESS_SEND_H
#define EGRESS_SEND_H

#include "EgressBase.h"
#include <PacketSlot.hpp>

#include <cstddef>
#include <cstdint>
#include <linux/if_packet.h>
#include <net/ethernet.h>

class EgressSend : public EgressBase
{
public:
    explicit EgressSend(Interface& iface, const TxQueueOpts& opts);
    ~EgressSend() override;

    bool send(uint32_t index, uint32_t length) noexcept override;

    void reclaim() override {}
    void cancel(uint32_t index) override;
    void flush() override {}
    void waitWritable() override {}

private:
    void mapFrame(uint32_t index, FrameHandle& out) override;

private:
    int sockFd = -1;
    uint8_t* frameArea = nullptr;
    uint32_t frameCount = 0;
    size_t frameStride = 0;
    sockaddr_ll addr{};
};

#endif // EGRESS_SEND_H
