// PacketSlot.hpp

#ifndef PACKET_SLOT_HPP
#define PACKET_SLOT_HPP

#include <cstdint>

namespace hardware
{

// PacketSlot is per-frame metadata stored in the TX ring buffer after the
// payload.  It is never serialised to the wire, so natural alignment is used
// to keep all fields aligned (no pragma pack).
struct PacketSlot
{
    uint32_t index;           // frame index in the EgressBase free ring
    uint8_t  dscp;            // DSCP value for QoS marking
    uint8_t  ecn;             // ECN bits
    uint8_t  cos;             // 802.1p Class of Service
    uint8_t  reserved = 0;    // explicit pad → len is 4-byte aligned
    uint32_t len;             // payload length in bytes
    uint32_t flowHash;        // per-flow hash for queue selection
    uint32_t classId;         // traffic class identifier
    // 4-byte implicit pad here → timestampNanos is 8-byte aligned
    uint64_t timestampNanos;  // enqueue timestamp (CLOCK_MONOTONIC)
};

struct FrameHandle
{
    PacketSlot* slot;
    uint8_t* payload;
    uint32_t qid;
};

} // namespace hardware

#endif

