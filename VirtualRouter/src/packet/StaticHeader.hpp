// StaticHeader.hpp

#ifndef STATIC_HEADER_HPP
#define STATIC_HEADER_HPP

#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace packet
{

struct StaticHeader
{
    uint8_t* buffer = nullptr;
    size_t totalLen = 0;

    StaticHeader() = default;

    StaticHeader(const uint8_t* src, size_t len)
        : totalLen(len)
    {
        if (totalLen == 0) return;
        buffer = static_cast<uint8_t*>(std::malloc(totalLen));
        if (buffer) std::memcpy(buffer, src, totalLen);
    }

    StaticHeader(const StaticHeader& other)
        : totalLen(other.totalLen)
    {
        if (!other.buffer || totalLen == 0) return;
        buffer = static_cast<uint8_t*>(std::malloc(totalLen));
        if (buffer) std::memcpy(buffer, other.buffer, totalLen);
    }

    StaticHeader& operator=(const StaticHeader& other)
    {
        if (this == &other) return *this;
        uint8_t* newBuf = nullptr;
        if (other.buffer && other.totalLen > 0)
        {
            newBuf = static_cast<uint8_t*>(std::malloc(other.totalLen));
            if (newBuf) std::memcpy(newBuf, other.buffer, other.totalLen);
        }
        std::free(buffer);
        buffer = newBuf;
        totalLen = other.totalLen;
        return *this;
    }

    StaticHeader(StaticHeader&& other) noexcept
        : buffer(other.buffer), totalLen(other.totalLen)
    {
        other.buffer = nullptr;
        other.totalLen = 0;
    }

    StaticHeader& operator=(StaticHeader&& other) noexcept
    {
        if (this == &other) return *this;
        if (other.buffer) std::free(buffer);
        buffer = other.buffer;
        totalLen = other.totalLen;
        other.buffer = nullptr;
        other.totalLen = 0;
        return *this;
    }

    ~StaticHeader()
    {
        std::free(buffer);
    }

    template <typename T>
    T get()
    {
        T hdr;
        if (!buffer || totalLen < T::fixedSize)
            return hdr;
        hdr.setBuffer(buffer);
        if constexpr (requires(T h) { h.getTrail(); })
        {
            size_t trailLen = (totalLen > T::fixedSize)
                ? totalLen - T::fixedSize
                : 0;
            hdr.setTrail(buffer + T::fixedSize, trailLen);
        }
        return hdr;
    }

    size_t copy(uint8_t* out, size_t maxSize) const
    {
        if (!buffer || totalLen == 0 || !out)
            return 0;

        size_t n = (totalLen <= maxSize) ? totalLen : 0;
        if (n == 0)
            return 0;

        std::memcpy(out, buffer, totalLen);
        return totalLen;
    }
};

} // namespace packet

#endif

