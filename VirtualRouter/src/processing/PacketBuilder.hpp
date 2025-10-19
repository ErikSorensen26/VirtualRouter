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

constexpr size_t MaxPacketSize = 2048;

struct BuildEntry
{
    HeaderType type;
    uint8_t* buffer;
    size_t length;
    HeaderType next = HeaderType::NONE;
};

class PacketBuilder
{
public:
    PacketBuilder(Interface* iface) : bufferOffset(0), buildIndex(0), headerCount(0), local(false)
    {
        FrameHandle frame;
        if (!iface->tx->getFrame(frame)) std::runtime_error("Full queue unhandled");
        slot = frame.slot;
        buffer = frame.payload;
    }

    PacketBuilder(FrameHandle& frame)
    {
        slot = frame.slot;
        buffer = frame.payload;
    }

    PacketBuilder(const PacketBuilder& other)
    {
        buffer = static_cast<uint8_t*>(std::malloc(MaxPacketSize));
        if (!buffer) throw std::bad_alloc();

        size_t usedBytes = other.bufferOffset;
        if (usedBytes > MaxPacketSize) usedBytes = MaxPacketSize;

        std::memcpy(buffer, other.buffer, usedBytes);

        headerCount = other.headerCount;
        buildIndex = other.buildIndex;
        bufferOffset = other.bufferOffset;
        local = true;
        slot = nullptr;

        std::memcpy(headers, other.headers, headerCount * sizeof(BuildEntry));

        // Rebase each header pointer into the new buffer
        for (size_t i = 0; i < headerCount; ++i)
        {
            ptrdiff_t offset = other.headers[i].buffer - other.buffer;
            if (offset < 0 || static_cast<size_t>(offset) >= usedBytes)
                headers[i].buffer = nullptr;
            else
                headers[i].buffer = buffer + offset;
        }
    }

    PacketBuilder& operator=(const PacketBuilder& other)
    {
        if (this != &other)
        {
            if (local && buffer)
                std::free(buffer);

            buffer = static_cast<uint8_t*>(std::malloc(MaxPacketSize));
            if (!buffer) throw std::bad_alloc();

            local = true;
            slot = nullptr;
            bufferOffset = other.bufferOffset;
            buildIndex = other.buildIndex;
            headerCount = other.headerCount;
            
            std::memcpy(buffer, other.buffer, other.bufferOffset);
            std::memcpy(headers, other.headers, sizeof(headers));

            for (size_t i = 0; i < headerCount; ++i) {
                ptrdiff_t offset = other.headers[i].buffer - other.buffer;
                headers[i].buffer = buffer + offset;
            }
        }
        return *this;
    }

    ~PacketBuilder()
    {
        if (local && buffer)
            std::free(buffer);
    }

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
        entry.buffer = buffer + bufferOffset;
        entry.length = size;
        slot->len += size;

        bufferOffset += size;
        return &entry;
    }

    void addTLVSize(size_t tlvSize)
    {
        bufferOffset += tlvSize;
        slot->len += tlvSize;
        currentBuildHeader()->length += tlvSize;
    }

    size_t getMaxHeaderSize(size_t mtu)
    {
        if (auto next = nextBuildHeader())
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

    BuildEntry* previewNextBuildHeader()
    {
        if (buildIndex >= headerCount)
            return nullptr;
        return &headers[headerCount - (buildIndex + 1)];
    }

    BuildEntry* currentBuildHeader()
    {
        return &headers[headerCount - buildIndex];
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
    uint8_t* getBuffer() { return buffer; }

    size_t bufferOffset = 0;
    PacketSlot* slot = nullptr;

private:
    uint8_t* buffer = nullptr;
    BuildEntry headers[MaxHeaders];
    size_t buildIndex = 0;
    size_t headerCount = 0;
    bool local = false;
};

#endif // PACKET_BUILDER_HPP

