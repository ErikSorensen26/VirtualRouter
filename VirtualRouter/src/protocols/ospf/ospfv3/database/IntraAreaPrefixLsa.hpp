// IntraAreaPrefixLsa.hpp

#ifndef INTRA_AREA_PREFIX_HPP
#define INTRA_AREA_PREFIX_HPP

#include <IPAddress.hpp>
#include <optional>
#include <HeaderHelpers.hpp>

namespace OSPF
{
struct IntraAreaPrefix
{
    uint8_t options;
    uint16_t metric;
    IPPrefix prefix;
};

struct IntraAreaPrefixLsa
{
    uint16_t referencesLsaType;
    uint32_t referencesLinkStateId;
    uint32_t referencedAdvRouter;
    std::vector<IntraAreaPrefix> prefixes;

    static std::optional<IntraAreaPrefixLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len > 12) return std::nullopt;

        uint16_t prefixes = readU16(buf);

        IntraAreaPrefixLsa lsa;
        lsa.referencesLsaType = readU16(buf + 2);
        lsa.referencesLinkStateId = readU32(buf + 4);
        lsa.referencedAdvRouter = readU32(buf + 8);

        size_t off = 12;
        for (uint16_t i = 0; i < prefixes; i++)
        {
            if (off + 4 > len) return std::nullopt;

            uint8_t plen = buf[off++];
            
            IntraAreaPrefix prefix;
            prefix.options = buf[off++];
            prefix.metric = readU16(buf + off);
            off += 2;

            uint8_t prefixBytes = (plen + 7) / 8;
            if (off + prefixBytes > len) return std::nullopt;

            std::memcpy(prefix.prefix.addr, buf + off, prefixBytes);
            prefix.prefix.af = AddressFamily::IPv6;
            prefix.prefix.prefixLength = plen;

            lsa.prefixes.push_back(prefix);
        }

        return lsa;
    }
};
}

#endif // INTRA_AREA_PREFIX_HPP
