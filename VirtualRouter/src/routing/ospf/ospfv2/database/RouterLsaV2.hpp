// RouterLsaV2.hpp

#ifndef ROUTER_LSA_V2_HPP
#define ROUTER_LSA_V2_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <algorithm>
#include <numeric>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
struct RouterLinkV2
{
    uint32_t linkId;
    uint32_t linkData;
    uint8_t type;
    uint16_t metric;

    bool operator==(const RouterLinkV2& rhs) const noexcept
    {
        return linkId == rhs.linkId &&
               linkData == rhs.linkData &&
               type == rhs.type &&
               metric == rhs.metric;
    }

    bool operator<(const RouterLinkV2& rhs) const noexcept
    {
        return std::tie(type, linkId, linkData, metric)
             < std::tie(rhs.type, rhs.linkId, rhs.linkData, rhs.metric);
    }
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
        uint16_t linkNum = utils::readU16(buf + 2);

        size_t offset = 4;

        for (int i = 0; i < linkNum; i++)
        {
            if (offset + 12 > len) return std::nullopt;

            RouterLinkV2 link;
            link.linkId = utils::readU32(buf + offset); offset += 4;
            link.linkData = utils::readU16(buf + offset); offset += 4;

            link.type = buf[offset++];
            uint8_t tosCount = buf[offset++];

            link.metric = utils::readU16(buf + offset); offset += 2;

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
        utils::writeU16(buf + 2, static_cast<uint16_t>(links.size()));

        size_t off = 4;
        for (auto& link : links)
        {
            utils::writeU32(buf + off, link.linkId);
            utils::writeU32(buf + off + 4, link.linkData);
            buf[off++] = link.type;
            buf[off++] = 0;
            utils::writeU16(buf + off, link.metric);
            off += 12;
        }
        return true;
    }

    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(4 * links.size());
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.add(flags);
        // Next byte is 0
        check.addU16(static_cast<uint16_t>(links.size()));
        for (const auto& link : links)
        {
            check.addU32(link.linkId);
            check.addU32(link.linkData);
            check.add(link.type);
            // Next byte is 0
            check.addU16(link.metric);
        }
    }

    bool operator==(const RouterLsaV2& rhs) const
    {
        if (flags != rhs.flags || links.size() != rhs.links.size())
            return false;

        std::vector<uint16_t> a(links.size()), b(rhs.links.size());

        std::iota(a.begin(), a.end(), 0);
        std::iota(b.begin(), b.end(), 0);

        std::sort(a.begin(), a.end(), [&](uint16_t i, uint16_t j) { return links[i] < links[j]; });
        std::sort(b.begin(), b.end(), [&](uint16_t i, uint16_t j) { return rhs.links[i] < rhs.links[j]; });

        for (size_t k = 0; k < a.size(); ++k)
            if (!(links[a[k]] != rhs.links[b[k]]))
                return false;

        return true;
    }
};
} // namespace routing

#endif // ROUTER_LSA_V2_HPP

