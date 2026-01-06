// ExternalLsaV3.hpp

#ifndef EXTERNAL_LSA_V3_HPP
#define EXTERNAL_LSA_V3_HPP

#include <IPAddress.hpp>
#include <HeaderHelpers.hpp>
#include <optional>

namespace OSPF
{
struct ExternalLsaV3
{
    uint32_t metric;
    uint8_t options;
    uint16_t referencedLsType;
    IPPrefix prefix;
    bool isType2;
    std::optional<IPAddress> forwardingAddress;
    std::optional<uint32_t> routeTag;
    std::optional<uint32_t> referencedLsId;

    static std::optional<ExternalLsaV3> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        ExternalLsaV3 lsa;

        uint8_t exOpts = buf[0];
        lsa.isType2 = (exOpts & 0x04) != 0;
        lsa.metric = readU24(buf + 1);

        uint8_t prefixLen = buf[4];
        lsa.options = buf[5];
        lsa.referencedLsType = readU16(buf + 6);

        if (prefixLen > 128) return std::nullopt;

        uint8_t prefixBytes = (prefixLen + 7) / 8;
        uint16_t off = 8;

        if (off + prefixBytes > len) return std::nullopt;

        std::memcpy(lsa.prefix.addr, buf + 8, prefixBytes);
        lsa.prefix.prefixLength = prefixLen;

        off += prefixBytes;

        if (exOpts & 0x02) // Forwarding flag
        {
            if (off + 16 > len) return std::nullopt;
            lsa.forwardingAddress.emplace(buf + off, AddressFamily::IPv6);
            off += 16;
        }
        if (exOpts & 0x01) // Route Tag Flag
        {
            if (off + 4 > len) return std::nullopt;
            lsa.routeTag = readU32(buf + off);
            off += 4;
        }
        if (lsa.referencedLsType != 0)
        {
            if (off + 4 > len) return std::nullopt;
            lsa.referencedLsId = readU32(buf + off);
            off += 4;
        }

        if (off != len) return std::nullopt;

        return lsa;
    }
};
}

#endif // EXTERNAL_LSA_V3_HPP
