// InterAreaPrefixLsa.hpp

#ifndef INTER_AREA_PREFIX_LSA_HPP
#define INTER_AREA_PREFIX_LSA_HPP

#include <cstdint>
#include <IPAddress.hpp>
#include <HeaderHelpers.hpp>
#include <optional>
#include <Functions.h>

namespace OSPF
{
struct InterAreaPrefixLsa
{
    uint32_t metric;
    uint8_t options;
    IPPrefix prefix;

    static std::optional<InterAreaPrefixLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        InterAreaPrefixLsa lsa;

        if (buf[0] != 0) return std::nullopt;
        lsa.metric = readU24(buf + 1);

        uint8_t prefixLen = buf[4];
        lsa.options = buf[5];
        if (readU16(buf + 5) != 0) return std::nullopt;

        uint8_t prefixWords = (prefixLen + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;

        if (prefixBytes > static_cast<uint8_t>(16)) return std::nullopt;
        if (8 + prefixBytes > len) return std::nullopt;

        std::memcpy(lsa.prefix.addr, buf + 8, prefixBytes);

        if (prefixLen % 8 != 0 && prefixBytes > 0)
        {
            uint8_t mask = 0xFF << (8 - (prefixLen % 8));
            lsa.prefix.addr[(prefixLen + 7) / 8 - 1] &= mask;
        }

        for (size_t i = 8 + prefixBytes; i < len; ++i)
        {
            if (buf[i] != 0) return std::nullopt;
        }

        lsa.prefix.prefixLength = prefixLen;
        lsa.prefix.af = AddressFamily::IPv6;

        return lsa;
    }
};
}

#endif // INTER_AREA_PREFIX_LSA_HPP
