// RouterLsaV3.hpp

#ifndef ROUTER_LSA_V3_HPP
#define ROUTER_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <HeaderHelpers.hpp>
#include <optional>
#include <OspfFletcher.hpp>
#include <algorithm>
#include <numeric>

namespace OSPF
{
struct RouterLinkV3
{
    uint8_t type;
    uint16_t metric;
    uint32_t interfaceId;
    uint32_t neighborInterfaceId;
    uint32_t neighborRouterId;

    bool operator==(const RouterLinkV3& rhs) const noexcept
    {
        return type == rhs.type &&
               metric == rhs.metric &&
               interfaceId == rhs.interfaceId &&
               neighborInterfaceId == rhs.neighborInterfaceId &&
               neighborRouterId == rhs.neighborRouterId;
    }

    bool operator<(const RouterLinkV3& rhs) const noexcept
    {
        return std::tie(type, interfaceId, neighborInterfaceId, neighborRouterId)
             < std::tie(rhs.type, rhs.interfaceId, rhs.neighborInterfaceId, rhs.neighborRouterId);
    }
};

struct RouterLsaV3
{
    uint32_t options;
    std::vector<RouterLinkV3> links;

    static std::optional<RouterLsaV3> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        RouterLsaV3 lsa;

        lsa.options = readU32(buf);

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

        writeU32(buf, options);

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

    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(16 * links.size());
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(options);
        for (const auto& link : links)
        {
            check.add(link.type);
            check.addU16(link.metric);
            check.addU32(link.interfaceId);
            check.addU32(link.neighborInterfaceId);
            check.addU32(link.neighborRouterId);
        }
    }

    bool operator==(const RouterLsaV3& rhs) const
    {
        if (options != rhs.options || links.size() != rhs.links.size())
            return false;

        std::vector<uint16_t> a(links.size()), b(rhs.links.size());

        std::iota(a.begin(), a.end(), 0);
        std::iota(b.begin(), b.end(), 0);

        std::sort(a.begin(), a.end(), [&](uint16_t i, uint16_t j) { return links[i] < links[j]; });
        std::sort(b.begin(), b.end(), [&](uint16_t i, uint16_t j) { return rhs.links[i] < rhs.links[j]; });

        for (size_t k = 0; k < a.size(); ++k)
            if (!(links[a[k]] == rhs.links[b[k]]))
                return false;

        return true;
    }
};
}

#endif // ROUTER_LSA_V3_HPP
