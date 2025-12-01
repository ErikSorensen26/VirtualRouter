// StaticHeader.hpp

#ifndef STATIC_HEADER_HPP
#define STATIC_HEADER_HPP

#include <cstdint>
#include <cstring>
#include <cstdlib>

#define RESTORE_FULL_HEADER(builder, staticHeader, headerObj, type)                         \
do {                                                                                        \
    BuildEntry* entry = (builder).reserveHeader(type, staticHeader.totalLen);               \
    std::memcpy(entry->buffer, staticHeader.buffer, (staticHeader).totalLen);               \
    (headerObj).setBuffer(entry->buffer);                                                   \
    if ((staticHeader).totalLen > (headerObj).fixedSize)                                    \
        (headerObj).setTrail(entry->buffer, (staticHeader).totalLen - headerObj.fixedSize); \
} while (0)

#define RESTORE_FIXED_HEADER(builder, staticHeader, headerObj, type)                        \
do {                                                                                        \
    BuildEntry* entry = builder.reserveHeader(type, staticHeader.totalLen);                 \
    std::memcpy(entry->buffer, staticHeader.buffer, staticHeader.totalLen);                 \
    (headerObj).setBuffer(entry->buffer);                                                   \
} while (0)

#define RESTORE

struct StaticHeader
{
    uint8_t* buffer = nullptr;
    size_t totalLen = 0;

    StaticHeader() = default;

    StaticHeader(const uint8_t* src, size_t len)
        : totalLen(len)
    {
        buffer = static_cast<uint8_t*>(std::malloc(totalLen));
        if (buffer) std::memcpy(buffer, src, totalLen);
    }

    StaticHeader(const StaticHeader& other)
        : totalLen(other.totalLen)
    {
        if (!other.buffer) return;
        buffer = static_cast<uint8_t*>(std::malloc(totalLen));
        if (buffer) std::memcpy(buffer, other.buffer, totalLen);
    }

    ~StaticHeader() {
        std::free(buffer);
    }
};

#endif
