/**
 * @file PacketBuilder.hpp
 */

// PacketBuilder.hpp

#ifndef PACKET_BUILDER_HPP
#define PACKET_BUILDER_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#include "hardware/PacketSlot.hpp"
#include "interface/Interface.h"
#include "qos/egress/TxDistributor.h"
#include "packet/StaticHeader.hpp"
#include "packet/PacketStructure.h"

namespace processing
{

constexpr size_t MaxPacketSize = 2048;

struct BuildEntry
{
    packet::HeaderType type;
    uint8_t*           buffer;
    size_t             length;
    packet::HeaderType next = packet::HeaderType::NONE;
};

class StaticPacket;

/**
 * RAII wrapper around a hardware TX frame.
 *
 * On construction the builder acquires a frame from the interface's TX
 * distributor.  If the builder is destroyed without the frame being
 * submitted (e.g. on an early return or exception), the frame is
 * automatically cancelled and returned to the free ring.
 *
 * Normal usage:
 *   PacketBuilder pkt(&iface);
 *   auto* eth = pkt.reserveHeader<EthernetHeader>(...);
 *   // ... fill headers ...
 *   pkt.send();     // submit; destructor is now a no-op
 *
 * Thread safety: one PacketBuilder per thread; not copyable.
 */
class PacketBuilder
{
public:
    // CONSTRUCTION

    PacketBuilder() = default;

    PacketBuilder(interface::Interface* iface,
                           qos::egress::TxDistPolicy policy   = qos::egress::TxDistPolicy::FLOW_HASH,
                           uint32_t                 flowHash  = 0)
        : tx(iface->tx)
    {
        if (!tx->getFrame(frame, policy, flowHash))
            throw std::runtime_error("PacketBuilder: TX queue full");
        clearBuild();
    }

    // Rebuild a previously saved packet template into a fresh frame.
    PacketBuilder(interface::Interface* iface,
                  const StaticPacket& saved,
                  qos::egress::TxDistPolicy policy  = qos::egress::TxDistPolicy::FLOW_HASH,
                  uint32_t              flowHash = 0);

    explicit PacketBuilder(hardware::FrameHandle& existingFrame)
        : frame(existingFrame) {}

    // OWNERSHIP

    // Non-copyable: a frame has exactly one owner.
    PacketBuilder(const PacketBuilder&)            = delete;
    PacketBuilder& operator=(const PacketBuilder&) = delete;

    // Move transfers frame ownership; the moved-from builder becomes inert.
    PacketBuilder(PacketBuilder&& o) noexcept
        : tx(o.tx),
          frame(o.frame),
          bufferOffset(o.bufferOffset),
          buildIndex(o.buildIndex),
          headerCount(o.headerCount)
    {
        std::memcpy(headers, o.headers, sizeof(headers));
        o.tx         = nullptr;
        o.frame.slot = nullptr; // prevent moved-from from cancelling
    }

    PacketBuilder& operator=(PacketBuilder&& o) noexcept
    {
        if (this != &o)
        {
            cancelFrame();          // release any frame we currently hold
            tx           = o.tx;
            frame        = o.frame;
            bufferOffset = o.bufferOffset;
            buildIndex   = o.buildIndex;
            headerCount  = o.headerCount;
            std::memcpy(headers, o.headers, sizeof(headers));
            o.tx         = nullptr;
            o.frame.slot = nullptr;
        }
        return *this;
    }

    // Destructor: if the frame was not submitted, cancel it automatically.
    ~PacketBuilder() { cancelFrame(); }

    // SUBMISSION

    void send()
    {
        if (!tx || !frame.slot) return;
        tx->send(frame);   // sets frame.slot = nullptr
    }

    void markSent() noexcept { frame.slot = nullptr; }

    void cancel() noexcept { cancelFrame(); }

    // HEADER BUILDING

    BuildEntry* reserveHeader(packet::HeaderType type, size_t size)
    {
        if (headerCount >= packet::MaxHeaders || bufferOffset + size > MaxPacketSize)
            return nullptr;

        if (headerCount != 0)
            headers[headerCount - 1].next = type;

        BuildEntry& entry = headers[headerCount++];
        entry.type   = type;
        entry.buffer = frame.payload + bufferOffset;
        entry.length = size;
        frame.slot->len += static_cast<uint32_t>(size);
        bufferOffset += size;
        return &entry;
    }

    template <typename T>
    T reserveAndBuildHeader(packet::HeaderType type)
    {
        constexpr size_t size = T::fixedSize;
        BuildEntry* entry = reserveHeader(type, size);
        if (!entry) return T{};
        ++buildIndex;
        T hdr;
        hdr.setBuffer(entry->buffer);
        return hdr;
    }

    BuildEntry* addHeader(const packet::StaticHeader& saved, packet::HeaderType type)
    {
        if (!saved.buffer || saved.totalLen == 0) return nullptr;
        BuildEntry* entry = reserveHeader(type, saved.totalLen);
        if (!entry) return nullptr;
        std::memcpy(entry->buffer, saved.buffer, saved.totalLen);
        ++buildIndex;
        return entry;
    }

    void addTLVSize(size_t tlvSize)
    {
        bufferOffset += tlvSize;
        frame.slot->len += static_cast<uint32_t>(tlvSize);
        currentBuildHeader()->length += tlvSize;
    }

    BuildEntry* getHeader(packet::HeaderType type)
    {
        for (auto& h : headers)
            if (h.type == type) return &h;
        return nullptr;
    }

    template <typename T>
    T getHeader(packet::HeaderType type)
    {
        return extractHeader<T>(getHeader(type));
    }

    size_t getMaxHeaderSize(size_t mtu)
    {
        if (auto* next = previewNextBuildHeader())
            return mtu - bufferOffset - packet::getHeaderSize(next->type);
        return mtu;
    }

    BuildEntry* nextBuildHeader()
    {
        if (buildIndex >= headerCount) return nullptr;
        return &headers[headerCount - (++buildIndex)];
    }

    template <typename T>
    T nextBuildHeader() { return extractHeader<T>(nextBuildHeader()); }

    BuildEntry* previewNextBuildHeader()
    {
        if (buildIndex >= headerCount) return nullptr;
        return &headers[headerCount - (buildIndex + 1)];
    }

    template <typename T>
    T previewNextBuildHeader() { return extractHeader<T>(previewNextBuildHeader()); }

    BuildEntry* currentBuildHeader()
    {
        return &headers[headerCount - buildIndex];
    }

    template <typename T>
    T currentBuildHeader() { return extractHeader<T>(previewNextBuildHeader()); }

    void clear() { clearBuild(); }

    const BuildEntry* getHeaders() const    { return headers; }
    size_t            getHeaderCount() const { return headerCount; }
    uint8_t*          getBuffer() const      { return frame.payload; }

    // PUBLIC DATA

    hardware::FrameHandle frame;
    size_t                bufferOffset = 0;

private:
    template <typename T>
    T extractHeader(BuildEntry* entry)
    {
        if (!entry) return T{};
        T hdr;
        hdr.setBuffer(entry->buffer);
        if constexpr (requires(T x) { x.getTrail(); })
        {
            size_t trail = (entry->length > T::fixedSize)
                           ? entry->length - T::fixedSize : 0;
            hdr.setTrail(entry->buffer + T::fixedSize, trail);
        }
        return hdr;
    }

    void clearBuild()
    {
        bufferOffset = 0;
        buildIndex   = 0;
        headerCount  = 0;
    }

    void cancelFrame() noexcept
    {
        if (tx && frame.slot)
        {
            tx->release(frame); // sets frame.slot = nullptr
        }
    }

    qos::egress::TxDistributor* tx = nullptr;

    BuildEntry headers[packet::MaxHeaders];
    size_t     buildIndex  = 0;
    size_t     headerCount = 0;
};

/**
 * @ingroup PROCESSING
 * @class StaticPacket
 *
 * Heap-allocated snapshot of a built packet (without owning a TX frame).
 * Used to pre-build packet templates that can be stamped into fresh frames.
 */
class StaticPacket
{
public:
    StaticPacket() = default;

    explicit StaticPacket(const PacketBuilder& builder)
    {
        bufferOffset = builder.bufferOffset;
        headerCount  = builder.getHeaderCount();
        buildIndex   = 0;

        size_t usedBytes = bufferOffset ? bufferOffset : 1;
        buffer = static_cast<uint8_t*>(std::malloc(usedBytes));
        if (!buffer) throw std::bad_alloc();

        std::memcpy(buffer, builder.getBuffer(), usedBytes);

        const BuildEntry* src = builder.getHeaders();
        for (size_t i = 0; i < headerCount; ++i)
        {
            headers[i].type   = src[i].type;
            headers[i].length = src[i].length;
            headers[i].next   = src[i].next;
            ptrdiff_t offset  = src[i].buffer - builder.getBuffer();
            headers[i].buffer = reinterpret_cast<uint8_t*>(offset);
        }
    }

    StaticPacket(const StaticPacket& other)  { copyFrom(other); }
    StaticPacket(      StaticPacket&& other) noexcept
        : buffer(other.buffer), bufferOffset(other.bufferOffset)
        , headerCount(other.headerCount), buildIndex(other.buildIndex)
    {
        std::memcpy(headers, other.headers, sizeof(headers));
        other.buffer = nullptr;
    }

    StaticPacket& operator=(const StaticPacket& other)
    {
        if (this != &other) copyFrom(other);
        return *this;
    }

    ~StaticPacket() { std::free(buffer); }

    const uint8_t*    getBuffer()      const noexcept { return buffer; }
    size_t            getBufferOffset() const noexcept { return bufferOffset; }
    size_t            getHeaderCount()  const noexcept { return headerCount; }
    const BuildEntry* getHeaders()      const noexcept { return headers; }

private:
    void copyFrom(const StaticPacket& other)
    {
        std::free(buffer);
        bufferOffset = other.bufferOffset;
        headerCount  = other.headerCount;
        buildIndex   = other.buildIndex;
        buffer = static_cast<uint8_t*>(std::malloc(bufferOffset ? bufferOffset : 1));
        if (!buffer) throw std::bad_alloc();
        std::memcpy(buffer, other.buffer, bufferOffset);
        std::memcpy(headers, other.headers, sizeof(headers));
    }

    uint8_t*   buffer       = nullptr;
    size_t     bufferOffset = 0;
    size_t     headerCount  = 0;
    size_t     buildIndex   = 0;
    BuildEntry headers[packet::MaxHeaders];
};

// ---- StaticPacket → PacketBuilder constructor (defined after StaticPacket) --
inline PacketBuilder::PacketBuilder(interface::Interface* iface,
                                     const StaticPacket& saved,
                                     qos::egress::TxDistPolicy policy,
                                     uint32_t flowHash)
    : tx(iface->tx)
{
    if (!tx->getFrame(frame, policy, flowHash))
        throw std::runtime_error("PacketBuilder: TX queue full when rebuilding from StaticPacket");

    bufferOffset = saved.getBufferOffset();
    buildIndex   = 0;
    headerCount  = saved.getHeaderCount();

    std::memcpy(frame.payload, saved.getBuffer(), bufferOffset);

    const BuildEntry* src = saved.getHeaders();
    for (size_t i = 0; i < headerCount; ++i)
    {
        headers[i].type   = src[i].type;
        headers[i].length = src[i].length;
        headers[i].next   = src[i].next;
        ptrdiff_t offset  = reinterpret_cast<ptrdiff_t>(src[i].buffer);
        headers[i].buffer = frame.payload + offset;
    }
}

} // namespace processing

#endif // PACKET_BUILDER_HPP
