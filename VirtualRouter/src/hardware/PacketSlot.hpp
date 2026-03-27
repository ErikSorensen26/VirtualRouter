/**
 * @file PacketSlot.hpp
 * @brief Per-frame TX metadata (QoS marks, flow hash, timestamp) and frame handle types.
 */

#ifndef PACKET_SLOT_HPP
#define PACKET_SLOT_HPP

#include <cstdint>

namespace hardware
{

/**
 * @brief Per-frame TX metadata stored in the TX ring buffer immediately after the payload.
 * @ingroup HARDWARE
 *
 * `PacketSlot` is never serialised to the wire; it is private metadata shared between
 * the QoS layer and the egress backend. Natural alignment is used so all fields are
 * properly aligned without `#pragma pack`.
 *
 * Layout (total 24 bytes before padding):
 * - Bytes  0–3  : `index`
 * - Bytes  4–7  : `dscp`, `ecn`, `cos`, `reserved`
 * - Bytes  8–11 : `len`
 * - Bytes 12–15 : `flowHash`
 * - Bytes 16–19 : `classId`
 * - 4-byte implicit pad (alignment to 8 bytes)
 * - Bytes 24–31 : `timestampNanos`
 *
 * @see FrameHandle, EgressBase
 */
struct PacketSlot
{
    uint32_t index;           ///< Frame index in the @ref EgressBase free ring; used to return the slot.
    uint8_t  dscp;            ///< DSCP value for IP QoS marking (6-bit field, stored in low bits).
    uint8_t  ecn;             ///< ECN bits (2-bit field, stored in low bits).
    uint8_t  cos;             ///< 802.1p Class of Service for VLAN-tagged frames.
    uint8_t  reserved = 0;   ///< Explicit pad to keep `len` 4-byte aligned; must remain zero.
    uint32_t len;             ///< Payload length in bytes (excludes PacketSlot itself).
    uint32_t flowHash;        ///< Per-flow hash used for queue selection and ECMP.
    uint32_t classId;         ///< Traffic class identifier assigned by the QoS classifier.
    // 4-byte implicit pad here ensures timestampNanos is 8-byte aligned.
    uint64_t timestampNanos;  ///< Enqueue timestamp from @c CLOCK_MONOTONIC (nanoseconds).
};

/**
 * @brief A borrowed TX frame: payload pointer, slot metadata, and queue ID.
 * @ingroup HARDWARE
 *
 * Returned by @ref EgressBase::getFrame(). The caller writes the packet into
 * `payload`, fills `slot` with QoS metadata, then calls @ref EgressBase::send()
 * using `slot->index`.
 *
 * @warning `payload` and `slot` point directly into the kernel-mapped TX ring.
 * Do not free or store them beyond the send call.
 */
struct FrameHandle
{
    PacketSlot* slot;    ///< Pointer to the @ref PacketSlot embedded after the payload in the ring.
    uint8_t*    payload; ///< Pointer to the writable payload region within the TX ring.
    uint32_t    qid;     ///< Hardware queue index this frame belongs to.
};

} // namespace hardware

#endif

