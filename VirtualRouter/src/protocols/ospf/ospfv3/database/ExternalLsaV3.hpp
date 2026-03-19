// ExternalLsaV3.hpp

#ifndef EXTERNAL_LSA_V3_HPP
#define EXTERNAL_LSA_V3_HPP

#include <IPAddress.hpp>
#include <optional>

#include "ospf/transmission/OspfFletcher.hpp"
#include "packet/HeaderHelpers.hpp"

namespace OSPF
{
struct ExternalLsaV3
{
    uint32_t metric;
    uint8_t options;
    uint16_t referencedLsType;
    IPv6Prefix prefix;
    bool isType2;
    std::optional<IPv6Address> forwardingAddress;
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

        lsa.prefix = IPv6Prefix(buf + 8, prefixLen);
        lsa.prefix.prefixLength = prefixLen;

        off += prefixBytes;

        if (exOpts & 0x02) // Forwarding flag
        {
            if (off + 16 > len) return std::nullopt;
            lsa.forwardingAddress.emplace(buf + off);
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

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 8) return false;

        uint8_t& exOpts = buf[0];
        if (isType2) exOpts |= 0x04;
        writeU24(buf + 1, metric);

        buf[4] = prefix.prefixLength;
        buf[5] = options;
        writeU16(buf + 6, referencedLsType);

        uint8_t prefixBytes = (prefix.prefixLength + 7) / 8;
        uint16_t off = 8;

        if (off + prefixBytes > len) return false;

        writeBytes(buf + 8, prefix.addr, prefixBytes);

        off += prefixBytes;

        if (forwardingAddress.has_value())
        {
            exOpts |= 0x02;
            if (off + 16 > len) return false;
            writeU128(buf + off, forwardingAddress.value().addr);
            off += 16;
        }
        if (routeTag.has_value())
        {
            exOpts |= 0x01;
            if (off + 4 > len) return false;
            writeU32(buf + off, routeTag.value());
            off += 4;
        }
        if (referencedLsType != 0)
        {
            if (off + 4 > len || !referencedLsId.has_value()) return false;
            writeU32(buf + off, referencedLsId.value());
        }

        return true;
    }

    inline uint16_t size() const
    {
        size_t len = 8 + (prefix.prefixLength + 7) / 8;
        if (forwardingAddress.has_value())
            len += 16;
        if (routeTag.has_value())
            len += 4;
        if (referencedLsId.has_value())
            len += 4;
        return len;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        uint8_t extOpts{0};
        if (isType2) extOpts |= 0x04;
        if (forwardingAddress.has_value()) extOpts |= 0x02;
        if (routeTag.has_value()) extOpts |= 0x01;

        check.addU24(metric);
        check.add(prefix.prefixLength);
        check.addU16(options);

        uint8_t prefixBytes = (prefix.prefixLength + 7) / 8;
        check.addBytes(prefix.raw(), prefixBytes);

        if (extOpts & 0x02)
            check.addBytes(forwardingAddress.value().raw(), 16);
        if (extOpts & 0x01)
            check.addU32(routeTag.value());
        if (referencedLsType != 0)
            check.addU32(referencedLsId.value());
    }
};
}

#endif // EXTERNAL_LSA_V3_HPP
