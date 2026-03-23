// InterAreaPrefixLsa.hpp

#ifndef INTER_AREA_PREFIX_LSA_HPP
#define INTER_AREA_PREFIX_LSA_HPP

#include <cstdint>
#include <IPAddress.h>
#include <optional>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
struct InterAreaPrefixLsa
{
    uint32_t metric;
    uint8_t options;
    types::IPv6Prefix prefix;

    static std::optional<InterAreaPrefixLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        InterAreaPrefixLsa lsa;

        if (buf[0] != 0) return std::nullopt;
        lsa.metric = utils::readU24(buf + 1);

        uint8_t prefixLen = buf[4];
        lsa.options = buf[5];
        if (utils::readU16(buf + 6) != 0) return std::nullopt;

        uint8_t prefixWords = (prefixLen + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;

        if (prefixBytes > static_cast<uint8_t>(16)) return std::nullopt;
        if (8 + prefixBytes > len) return std::nullopt;

        lsa.prefix = types::IPv6Prefix(buf + 8, prefixLen);

        for (size_t i = 8 + prefixBytes; i < len; ++i)
        {
            if (buf[i] != 0) return std::nullopt;
        }

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 8) return false;

        buf[0] = 0;
        utils::writeU24(buf + 1, metric);
        buf[4] = prefix.prefixLength;
        buf[5] = options;
        buf[6] = 0;
        buf[7] = 0;

        uint8_t prefixWords = (prefix.prefixLength + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;
        if (8 + prefixBytes > len) return false;
        utils::writeBytes(buf + 8, prefix.addr, prefixBytes);

        return true;
    }

    inline uint16_t size() const
    {
        uint8_t prefixWords = (prefix.prefixLength + 31) / 32;
        return 8 + (prefixWords * 4);
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU24(metric);
        check.add(prefix.prefixLength);
        check.add(options);

        uint8_t prefixBytes = ((prefix.prefixLength + 31) / 32) * 4;
        for (size_t i = 0; i < 16 || i < prefixBytes; ++i)
        {
            check.add(prefix.raw()[i]);
        }
    }
};
} // namespace routing

#endif // INTER_AREA_PREFIX_LSA_HPP

