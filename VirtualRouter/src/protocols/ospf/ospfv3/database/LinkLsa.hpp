// LinkLsa.hpp

#ifndef LINK_LSA_HPP
#define LINK_LSA_HPP

#include <IPAddress.h>
#include <optional>

#include "ospf/transmission/OspfFletcher.hpp"
#include "packet/HeaderHelpers.hpp"

namespace OSPF
{
struct LinkLsaPrefix
{
    uint8_t options;
    IPv6Prefix prefix;
};

struct LinkLsa
{
    uint8_t priority;
    uint32_t options;
    IPv6Address localLink;
    std::vector<LinkLsaPrefix> prefixes;

    static std::optional<LinkLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 20) return std::nullopt;

        LinkLsa lsa;

        lsa.priority = buf[0];
        lsa.options = readU24(buf + 1);
        lsa.localLink = IPv6Address(buf + 4);

        uint8_t prefixList = buf[20];
        size_t off = 21;
        for (uint8_t i = 0; i < prefixList; i++)
        {
            if (off + 2 > len) return std::nullopt;
            LinkLsaPrefix link;
            uint8_t prefixLen = buf[off++];
            link.options = buf[off++];
            uint8_t prefixBytes = (prefixLen + 7) / 8;

            if (off + prefixBytes > len) return std::nullopt;
            link.prefix = IPv6Prefix(buf + off, prefixLen);
            lsa.prefixes.push_back(link);
        }

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 20) return false;

        buf[0] = priority;
        writeU24(buf + 1, options);
        writeU128(buf + 4, localLink.addr);

        buf[20] = static_cast<uint8_t>(prefixes.size());
        size_t off = 21;
        for (const auto& link : prefixes)
        {
            if (off + 2 > len) return false;
            buf[off++] = link.prefix.prefixLength;
            buf[off++] = link.options;
            uint8_t prefixBytes = (link.prefix.prefixLength + 7) / 8;

            if (off + prefixBytes > len) return false;
            writeBytes(buf + off, link.prefix.addr, prefixBytes);
        }

        return true;
    }

    inline uint16_t size() const
    {
        uint16_t len = 20;
        for (const auto& link : prefixes)
        {
            len += 2 + ((link.prefix.prefixLength + 7) / 8);
        }
        return len;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.add(priority);
        check.addU24(options);
        check.addBytes(localLink.raw(), 16);
        for (const auto& link : prefixes)
        {
            check.add(link.prefix.prefixLength);
            check.add(link.options);
            uint8_t prefixBytes = (link.prefix.prefixLength + 7) / 8;
            check.addBytes(link.prefix.raw(), prefixBytes);
        }
    }
};
}

#endif // LINK_LSA_HPP
