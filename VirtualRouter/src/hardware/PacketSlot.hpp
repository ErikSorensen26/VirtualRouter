// PacketSlot.hpp

#ifndef PACKET_SLOT_HPP
#define PACKET_SLOT_HPP

#include <cstdint>

namespace hardware
{

#pragma pack(push, 1)
struct PacketSlot
{
    uint32_t index;
    uint8_t dscp;
    uint8_t ecn;
    uint8_t cos;
    uint32_t len;
    uint32_t flowHash;
    uint32_t classId;
    uint64_t timestampNanos;
};
#pragma pack(pop)

struct FrameHandle
{
    PacketSlot* slot;
    uint8_t* payload;
    uint32_t qid;
};

} // namespace hardware

#endif

