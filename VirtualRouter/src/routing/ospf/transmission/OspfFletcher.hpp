// OspfFletcher.hpp

#ifndef OSPF_FLETCHER_HPP
#define OSPF_FLETCHER_HPP

#include <cstdint>
#include <cstddef>
#include <NetworkSpan.hpp>

namespace OSPF
{
class ChecksumFletcher
{
public:
    uint16_t c0 = 0;
    uint16_t c1 = 0;

    inline void add(uint8_t b)
    {
        c0 += b;
        if (c0 >= 255) c0 -= 255;
        c1 += c0;
        if (c1 >= 255) c1 -= 255;
    }

    inline void addU32(uint32_t b)
    {
        add(static_cast<uint8_t>(b >> 24));
        add(static_cast<uint8_t>((b >> 16) & 0xFF));
        add(static_cast<uint8_t>((b >> 8) & 0xFF));
        add(static_cast<uint8_t>(b & 0xFF));
    }

    inline void addU24(uint32_t b)
    {
        add(static_cast<uint8_t>((b >> 16) & 0xFF));
        add(static_cast<uint8_t>((b >> 8) & 0xFF));
        add(static_cast<uint8_t>(b & 0xFF));
    }

    inline void addU16(uint16_t b)
    {
        add(static_cast<uint8_t>((b >> 8) & 0xFF));
        add(static_cast<uint8_t>(b & 0xFF));
    }

    inline void addBytes(const uint8_t* data, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            add(data[i]);
        }
    }

    template <typename N>
    inline void addBytes(const NetworkSpan<N>& data, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            add(data[i]);
        }
    }

    inline uint16_t finalize() const
    {
        return static_cast<uint16_t>((c1 << 8) | c0);
    }
};
}

#endif
