// NetworkLsaV3

#ifndef NETWORK_LSA_V3_HPP
#define NETWORK_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <algorithm>

#include "ospf/transmission/OspfFletcher.hpp"
#include "packet/HeaderHelpers.hpp"

namespace OSPF
{
struct NetworkLsaV3
{
    uint32_t options;
    std::vector<uint32_t> attachedRouters;

    static std::optional<NetworkLsaV3> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        NetworkLsaV3 lsa;

        lsa.options = readU24(buf + 1);
        size_t off = 4;

        if ((len - off) % 4 != 0) return std::nullopt;

        while (off < len)
        {
            lsa.attachedRouters.push_back(readU32(buf + off));
            off += 4;
        }

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 4) return false;

        writeU24(buf + 1, options);
        size_t off = 4;
        
        if (4 + (4 * attachedRouters.size()) != len) return false;

        for (const auto& router : attachedRouters)
        {
            writeU32(buf + off, router);
            off += 4;
        }

        return true;
    }

    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(4 * attachedRouters.size());
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU24(options);
        for (const auto& router : attachedRouters)
            check.addU32(router);
    }

    bool operator==(const NetworkLsaV3& lsa) const
    {
        if (attachedRouters.size() != lsa.attachedRouters.size() || options != lsa.options)
            return false;
        auto a = attachedRouters;
        auto b = lsa.attachedRouters;

        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());

        return a == b;
    }
};
}

#endif // NETWORK_LSA_V3_HPP
