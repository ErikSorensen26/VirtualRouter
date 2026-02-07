// NetworkLsaV2.hpp

#ifndef NETWORK_LSA_V2_HPP
#define NETWORK_LSA_V2_HPP

#include <cstdint>
#include <vector>
#include <HeaderHelpers.hpp>
#include <optional>
#include <algorithm>
#include <OspfFletcher.hpp>

namespace OSPF
{
struct NetworkLsaV2
{
    uint32_t networkMask;
    std::vector<uint32_t> attachedRouters;

    static std::optional<NetworkLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        NetworkLsaV2 lsa;
        
        lsa.networkMask = readU32(buf);
        size_t offset = 4;

        if ((len - offset) % 4 != 0)
            return std::nullopt;

        while (offset + 4 <= len)
        {
            uint32_t rid = readU32(buf + offset);
            lsa.attachedRouters.push_back(rid);
            offset += 4;
        }

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if ((attachedRouters.size() * 4) + 4 != len)
            return false;

        writeU32(buf, networkMask);
        size_t off = 4;
        for (auto& r : attachedRouters)
        {
            writeU32(buf + off, r);
            off += 4;
        }
        return true;
    }

    inline uint16_t size() const
    {
        return static_cast<uint16_t>(4 + (4 * attachedRouters.size()));
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(networkMask);
        for (auto& r : attachedRouters)
            check.addU32(r);
    }

    bool operator==(const NetworkLsaV2& lsa) const
    {
        if (attachedRouters.size() != lsa.attachedRouters.size() || networkMask != lsa.networkMask)
            return false;
        auto a = attachedRouters;
        auto b = lsa.attachedRouters;

        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());

        return a == b;
    }
};
}

#endif // NETWORK_LSA_V2_HPP
