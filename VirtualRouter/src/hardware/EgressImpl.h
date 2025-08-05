// EgressImpl.h

#ifndef EGRESS_IMPL_H
#define EGRESS_IMPL_H

#include <PacketSlot.hpp>

class EgressImpl
{
public:
    virtual ~EgressImpl() = default;
    virtual FrameHandle getFrame() = 0;
    virtual bool send(uint32_t index, uint32_t length) noexcept = 0;
    virtual void releaseFrame(uint32_t index) = 0;
    virtual void reclaim() = 0;

}

#endif // EGRESS_IMPL_H
