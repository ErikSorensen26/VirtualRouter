// PacketBuilder.hpp

#ifndef PACKET_BUILDER_HPP
#define PACKET_BUILDER_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <PacketStructure.h>

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
    PacketBuilder() : bufferOffset(0), buildIndex(0), headerCount(0) {}

    PacketBuilder(const PacketBuilder& other)
        : bufferOffset(other.bufferOffset),
          buildIndex(other.buildIndex),
          headerCount(other.headerCount)
    {
        std::memcpy(buffer, other.buffer, MaxPacketSize);
        std::memcpy(headers, other.headers, sizeof(headers));

        // Rebase each header pointer into the new buffer
        for (size_t i = 0; i < headerCount; ++i) {
            ptrdiff_t offset = other.headers[i].buffer - other.buffer;
            headers[i].buffer = buffer + offset;
        }
    }

    PacketBuilder& operator=(const PacketBuilder& other)
    {
        if (this != &other)
        {
            bufferOffset = other.bufferOffset;
            buildIndex = other.buildIndex;
            headerCount = other.headerCount;
            
            std::memcpy(buffer, other.buffer, MaxHeaders);
            std::memcpy(headers, other.headers, sizeof(headers));

            for (size_t i = 0; i < headerCount; ++i) {
                ptrdiff_t offset = other.headers[i].buffer - other.buffer;
                headers[i].buffer = buffer + offset;
            }
        }
        return *this;
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

        bufferOffset += size;
        return &entry;
    }

    void addTLVSize(size_t tlvSize) {
        bufferOffset += tlvSize;
        currentBuildHeader()->length += tlvSize;
    }

    size_t getMaxHeaderSize(size_t mtu) {
        if (auto next = nextBuildHeader())
        {
            return mtu - bufferOffset - getHeaderSize(next->type);
        }
    }

    // Get the next header in the build order (top-down)
    BuildEntry* nextBuildHeader()
    {
        if (buildIndex >= headerCount)
            return nullptr;
        return &headers[headerCount - (++buildIndex)];
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
    size_t bufferOffset;

private:
    alignas(8) uint8_t buffer[MaxPacketSize];
    BuildEntry headers[MaxHeaders];
    size_t buildIndex;
    size_t headerCount;
};

#endif // PACKET_BUILDER_HPP

