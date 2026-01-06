// RouterLsaV3.hpp

#ifndef ROUTER_LSA_V3_HPP
#define ROUTER_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <HeaderHelpers.hpp>
#include <optional>

namespace OSPF
{
struct RouterLinkV3
{
    uint8_t type;
    uint32_t metric;
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
};
}

#endif // ROUTER_LSA_V3_HPP
