// RouterLsaV2.hpp

#ifndef ROUTER_LSA_V2_HPP
#define ROUTER_LSA_V2_HPP

#include <cstdint>
#include <vector>
#include <HeaderHelpers.hpp>
#include <optional>

namespace OSPF
{
struct RouterLinkV2
{
    uint32_t linkId;
    uint32_t linkData;
    uint8_t type;
    uint16_t metric;
};

struct RouterLsaV2
{
    uint8_t flags;
    std::vector<RouterLinkV2> links;

    // Build the LSA
    static std::optional<RouterLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        RouterLsaV2 lsa;

        lsa.flags = buf[0];
        uint16_t linkNum = readU16(buf + 2);

        size_t offset = 4;

        for (int i = 0; i < linkNum; i++)
        {
            if (offset + 12 > len) return std::nullopt;

            RouterLinkV2 link;
            link.linkId = readU32(buf + offset); offset += 4;
            link.linkData = readU16(buf + offset); offset += 4;

            link.type = buf[offset++];
            uint8_t tosCount = buf[offset++];

            link.metric = readU16(buf + offset); offset += 2;

            size_t tosBytes = static_cast<size_t>(tosCount) * 4;
            if (offset + tosBytes > len) return std::nullopt;

            offset += tosBytes;

            lsa.links.push_back(link);
        }

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if ((links.size() * 12) + 4 != len) return false;

        buf[0] = flags;
        buf[1] = 0;
        writeU16(buf + 2, static_cast<uint16_t>(links.size()));

        size_t off = 4;
        for (auto& link : links)
        {
            writeU32(buf + off, link.linkId);
            writeU32(buf + off + 4, link.linkData);
            buf[off++] = link.type;
            buf[off++] = 0;
            writeU16(buf + off, link.metric);
            off += 12;
        }
        return true;
    }
};
}

#endif // ROUTER_LSA_V2_HPP
