// RouterLsaV3.hpp

#ifndef ROUTER_LSA_V3_HPP
#define ROUTER_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <HeaderHelpers.hpp>
#include <optional>
#include <OspfFletcher.hpp>

namespace OSPF
{
struct RouterLinkV3
{
    uint8_t type;
    uint16_t metric;
    uint32_t interfaceId;
    uint32_t neighborInterfaceId;
    uint32_t neighborRouterId;
};

struct RouterLsaV3
{
    uint8_t flags;
    uint32_t options;
    std::vector<RouterLinkV3> links;

    static std::optional<RouterLsaV3> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        RouterLsaV3 lsa;

        lsa.flags = buf[0];
        lsa.options = readU24(buf + 1);

        size_t off = 4;

        while (off + 16 <= len)
        {
            RouterLinkV3 link;
            link.type = buf[off];
            link.metric = readU16(buf + off + 2);
            link.interfaceId = readU32(buf + off + 4);
            link.neighborInterfaceId = readU32(buf + off + 8);
            link.neighborRouterId = readU32(buf + off + 12);
            lsa.links.push_back(link);
            off += 16;
        }

        if (off != len) return std::nullopt;
        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != (4 + (16 * links.size()))) return false;

        buf[0] = flags;
        writeU24(buf + 1, options);

        size_t off = 4;
        for (const auto& link : links)
        {
            buf[off++] = link.type; 
            buf[off++] = 0;
            writeU16(buf + off, link.metric); off += 2;
            writeU32(buf + off, link.interfaceId); off += 4;
            writeU32(buf + off, link.neighborInterfaceId); off += 4;
            writeU32(buf + off, link.neighborRouterId); off += 4;
        }

        return true;
    }

    inline size_t size() const
    {
        return 4 + (16 * links.size());
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.add(flags);
        check.addU24(options);
        for (const auto& link : links)
        {
            check.add(link.type);
            check.addU16(link.metric);
            check.addU32(link.interfaceId);
            check.addU32(link.neighborInterfaceId);
            check.addU32(link.neighborRouterId);
        }
    }
};
}

#endif // ROUTER_LSA_V3_HPP
