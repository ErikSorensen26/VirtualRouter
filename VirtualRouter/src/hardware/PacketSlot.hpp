// PacketSlot.hpp

#ifndef PACKET_SLOT_HPP
#define PACKET_SLOT_HPP

#include <cstdint>

struct PacketSlot
{
    const uint32_t index;
    uint8_t dscp;
    uint8_t ecn;
    uint8_t cos;
    uint32_t lengthBytes;
    uint32_t flowHash;
    uint32_t classId;
    uint64_t timestampNanos;
};

struct FrameHandle
{
    PacketSlot* slot;
    uint8_t* payload;
};

#endif // PACKET_SLOT_HPP
