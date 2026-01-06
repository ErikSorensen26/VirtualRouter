// LinkLsa.hpp

#ifndef LINK_LSA_HPP
#define LINK_LSA_HPP

#include <IPAddress.hpp>
#include <HeaderHelpers.hpp>
#include <optional>

namespace OSPF
{
struct LinkLsaPrefix
{
    uint8_t options;
    IPPrefix prefix;
};

struct LinkLsa
{
    uint8_t priority;
    uint32_t options;
    IPAddress localLink;
    std::vector<LinkLsaPrefix> prefixes;

    static std::optional<LinkLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 20) return std::nullopt;

        LinkLsa lsa;

        lsa.priority = buf[0];
        lsa.options = readU24(buf + 1);
        std::memcpy(lsa.localLink.raw, buf + 4, 16);
        lsa.localLink.isV6 = true;

        uint8_t prefixList = buf[20];
        size_t off = 21;
        for (uint8_t i = 0; i < prefixList; i++)
        {
            LinkLsaPrefix link;
            uint8_t prefixLen = buf[off++];
            link.options = buf[off++];
            uint8_t prefixBytes = (prefixLen + 7) / 8;

            std::memcpy(link.prefix.addr, buf + off, prefixBytes);
            link.prefix.af = AddressFamily::IPv6;
            link.prefix.prefixLength = prefixLen;
            lsa.prefixes.push_back(link);
        }

        return lsa;
    }
};
}

#endif // LINK_LSA_HPP
