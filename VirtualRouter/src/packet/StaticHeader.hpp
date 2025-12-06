// StaticHeader.hpp

#ifndef STATIC_HEADER_HPP
#define STATIC_HEADER_HPP

#include <cstdint>
#include <cstring>
#include <cstdlib>

struct StaticHeader
{
    uint8_t* buffer = nullptr;
    size_t totalLen = 0;

    StaticHeader() = default;

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
