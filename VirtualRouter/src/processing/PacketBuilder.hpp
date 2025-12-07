// PacketBuilder.hpp

#ifndef PACKET_BUILDER_HPP
#define PACKET_BUILDER_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <PacketStructure.h>
#include <PacketSlot.hpp>
#include <Interface.h>
#include <TxDistributor.h>
#include <StaticHeader.hpp>

constexpr size_t MaxPacketSize = 2048;

struct BuildEntry
{
    HeaderType type;
    uint8_t* buffer;
    size_t length;
    HeaderType next = HeaderType::NONE;
};

class StaticPacket;

class PacketBuilder
{
public:
    PacketBuilder() = default;

    PacketBuilder(Interface* iface) : bufferOffset(0), buildIndex(0), headerCount(0)
    {
        if (!iface->tx->getFrame(frame)) throw std::runtime_error("Full queue unhandled");
    }

    PacketBuilder(FrameHandle& frame)
        : frame(frame) {}

    PacketBuilder(Interface* iface, const StaticPacket& saved);

    ~PacketBuilder() = default;

    // Reserve space for a new header from the start of the buffer
    BuildEntry* reserveHeader(HeaderType type, size_t size)
    {
        if (headerCount >= MaxHeaders || bufferOffset + size > MaxPacketSize)
            return nullptr;

        if (headerCount != 0) {
            headers[headerCount - 1].next = type;
        }

        BuildEntry& entry = headers[headerCount++];
        entry.type = type;
        entry.buffer = frame.payload + bufferOffset;
        entry.length = size;
        frame.slot->len += size;

        bufferOffset += size;
        return &entry;
    }

    template <typename T>
    T reserveAndBuildHeader(HeaderType type)
    {
        constexpr size_t size = T::fixedSize;

        BuildEntry* entry = reserveHeader(type, size);
        if (!entry) return T{};
        buildIndex++;

        T hdr;
        hdr.setBuffer(entry->buffer);
        return hdr;
    }

    BuildEntry* getHeader(HeaderType type)
    {
        for (auto& header : headers)
        {
            if (header.type == type)
                return &header;
        }
        return nullptr;
    }

    template <typename T>
    T getHeader(HeaderType type)
    {
        BuildEntry* entry = getHeader(type);
        return extractHeader<T>(entry);
    }

    BuildEntry* addHeader(const StaticHeader& saved, HeaderType type)
    {
        if (!saved.buffer || saved.totalLen == 0)
            return nullptr;

        BuildEntry* entry = reserveHeader(type, saved.totalLen);
        if (!entry)
            return nullptr;

        std::memcpy(entry->buffer, saved.buffer, saved.totalLen);
        ++buildIndex;

        return entry;
    }

    void addTLVSize(size_t tlvSize)
    {
        bufferOffset += tlvSize;
        frame.slot->len += tlvSize;
        currentBuildHeader()->length += tlvSize;
    }

    size_t getMaxHeaderSize(size_t mtu)
    {
        if (auto next = previewNextBuildHeader())
            return mtu - bufferOffset - getHeaderSize(next->type);
        return mtu;
    }

    // Get the next header in the build order (top-down)
    BuildEntry* nextBuildHeader()
    {
        if (buildIndex >= headerCount)
            return nullptr;
        return &headers[headerCount - (++buildIndex)];
    }

    template <typename T>
    T nextBuildHeader()
    {
        BuildEntry* entry = nextBuildHeader();
        return extractHeader<T>(entry);
    }

    BuildEntry* previewNextBuildHeader()
    {
        if (buildIndex >= headerCount)
            return nullptr;
        return &headers[headerCount - (buildIndex + 1)];
    }

    template <typename T>
    T previewNextBuildHeader()
    {
        BuildEntry* entry = previewNextBuildHeader();
        return extractHeader<T>(entry);
    }

    BuildEntry* currentBuildHeader()
    {
        return &headers[headerCount - buildIndex];
    }

    template <typename T>
    T currentBuildHeader()
    {
        BuildEntry* entry = previewNextBuildHeader();
        return extractHeader<T>(entry);
    }

    // Reset for reuse
    void clear()
    {
        bufferOffset = 0;
        buildIndex = 0;
        headerCount = 0;
    }

    // Direct access to all headers
    const BuildEntry* getHeaders() const { return headers; }
    size_t getHeaderCount() const { return headerCount; }
    uint8_t* getBuffer() const { return frame.payload; }

    FrameHandle frame;
    size_t bufferOffset = 0;

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
                ? entry->length - T::fixedSize
                : 0;
            hdr.setTrail(entry->buffer + T::fixedSize, trail);
        }

        return hdr;
    }

    BuildEntry headers[MaxHeaders];
    size_t buildIndex = 0;
    size_t headerCount = 0;
};

class StaticPacket
{
public:
    StaticPacket() = default;

    explicit StaticPacket(const PacketBuilder& builder)
    {
        bufferOffset = builder.bufferOffset;
        headerCount = builder.getHeaderCount();
        buildIndex = 0;

        size_t usedBytes = bufferOffset;
        if (usedBytes == 0) usedBytes = 1;

        buffer = static_cast<uint8_t*>(std::malloc(usedBytes));
        if (!buffer) throw std::bad_alloc();
        
        std::memcpy(buffer, builder.getBuffer(), usedBytes);

        const BuildEntry* src = builder.getHeaders();

        for (size_t i = 0; i < headerCount; ++i)
        {
            headers[i].type = src[i].type;
            headers[i].length = src[i].length;
            headers[i].next = src[i].next;

            ptrdiff_t offset = src[i].buffer - builder.getBuffer();
            headers[i].buffer = reinterpret_cast<uint8_t*>(offset);
        }
    }

    StaticPacket(const StaticPacket& other)
    {
        copyFrom(other);
    }

    StaticPacket& operator=(const StaticPacket& other)
    {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    ~StaticPacket()
    {
        if (buffer)
            std::free(buffer);
    }

    const uint8_t* getBuffer() const noexcept { return buffer; }
    size_t getBufferOffset() const noexcept { return bufferOffset; }
    size_t getHeaderCount() const noexcept { return headerCount; }
    const BuildEntry* getHeaders() const noexcept { return headers; }

private:
    void copyFrom(const StaticPacket& other)
    {
        if (buffer)
            std::free(buffer);

        bufferOffset = other.bufferOffset;
        headerCount = other.headerCount;
        buildIndex = other.buildIndex;

        buffer = static_cast<uint8_t*>(std::malloc(bufferOffset));
        if (!buffer)
            throw std::bad_alloc();
        std::memcpy(buffer, other.buffer, bufferOffset);
        std::memcpy(headers, other.headers, sizeof(headers));
    }

    uint8_t* buffer = nullptr;
    size_t bufferOffset = 0;
    size_t headerCount = 0;
    size_t buildIndex = 0;

    BuildEntry headers[MaxHeaders];
};

inline PacketBuilder::PacketBuilder(Interface* iface, const StaticPacket& saved)
{
    if (!iface->tx->getFrame(frame)) throw std::runtime_error("Full TX queue when rebuilding from StaticPacket");

    bufferOffset = saved.getBufferOffset();
    buildIndex = 0;
    headerCount = saved.getHeaderCount();

    std::memcpy(frame.payload, saved.getBuffer(), bufferOffset);

    const BuildEntry* src = saved.getHeaders();
    for (size_t i = 0; i < headerCount; ++i)
    {
        headers[i].type = src[i].type;
        headers[i].length = src[i].length;
        headers[i].next = src[i].next;

        ptrdiff_t offset = reinterpret_cast<ptrdiff_t>(src[i].buffer);
        headers[i].buffer = frame.payload + offset;
    }
}

#endif // PACKET_BUILDER_HPP

