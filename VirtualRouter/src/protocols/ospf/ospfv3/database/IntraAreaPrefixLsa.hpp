// IntraAreaPrefixLsa.hpp

#ifndef INTRA_AREA_PREFIX_HPP
#define INTRA_AREA_PREFIX_HPP

#include <IPAddress.h>
#include <optional>

#include "ospf/transmission/OspfFletcher.hpp"
#include "packet/HeaderHelpers.hpp"

namespace OSPF
{
struct IntraAreaPrefix
{
    uint8_t options;
    uint16_t metric;
    IPv6Prefix prefix;

    void setNoUnicast(bool val)
        { setBit(&options, 7, val); }
    void setLocalAddress(bool val)
        { setBit(&options, 6, val); }
    void setMulticast(bool val)
        { setBit(&options, 5, val); }
    void setPropagate(bool val)
        { setBit(&options, 4, val); }

    bool operator==(const IntraAreaPrefix& rhs) const noexcept
    {
        return options == rhs.options &&
               metric == rhs.metric &&
               prefix == rhs.prefix;
    }
};

struct IntraAreaPrefixLsa
{
    uint16_t referencedLsaType;
    uint32_t referencedLinkStateId;
    uint32_t referencedAdvRouter;
    std::vector<IntraAreaPrefix> prefixes;

    static std::optional<IntraAreaPrefixLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len > 12) return std::nullopt;

        uint16_t prefixes = readU16(buf);

        IntraAreaPrefixLsa lsa;
        lsa.referencedLsaType = readU16(buf + 2);
        lsa.referencedLinkStateId = readU32(buf + 4);
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

            prefix.prefix = IPv6Prefix(buf + off, plen);

            lsa.prefixes.push_back(prefix);
        }

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len > 12) return false;

        writeU16(buf, static_cast<uint16_t>(prefixes.size()));
        writeU16(buf + 2, referencedLsaType);
        writeU32(buf + 4, referencedLinkStateId);
        writeU32(buf + 8, referencedAdvRouter);

        size_t off = 12;
        for (const auto& prefix : prefixes)
        {
            if (off + 4 > len) return false;

            buf[off++] = prefix.prefix.prefixLength;
            buf[off++] = prefix.options;
            writeU16(buf + off, prefix.metric);
            off += 2;

            uint8_t prefixBytes = (prefix.prefix.prefixLength + 7) / 8;
            if (off + prefixBytes > len) return false;

            writeBytes(buf + off, prefix.prefix.addr, prefixBytes);
        }

        return true;
    }

    inline uint16_t size() const
    {
        uint16_t len = 12;
        for (const auto& prefix : prefixes)
        {
            len += (4 + ((prefix.prefix.prefixLength + 7) / 8));
        }
        return len;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU16(static_cast<uint16_t>(prefixes.size()));
        check.addU16(referencedLsaType);
        check.addU32(referencedLinkStateId);
        check.addU32(referencedAdvRouter);

        for (const auto& prefix : prefixes)
        {
            check.add(prefix.prefix.prefixLength);
            check.add(prefix.options);
            check.addU16(prefix.metric);

            uint8_t prefixBytes = (prefix.prefix.prefixLength + 7) / 8;
            check.addBytes(prefix.prefix.raw(), prefixBytes);
        }
    }
};
}

#endif // INTRA_AREA_PREFIX_HPP
