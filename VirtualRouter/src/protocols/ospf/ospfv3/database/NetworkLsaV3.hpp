// NetworkLsaV3

#ifndef NETWORK_LSA_V3_HPP
#define NETWORK_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <HeaderHelpers.hpp>
#include <optional>

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

        lsa.options = readU24(buf);
        size_t off = 4;

        if ((len - off) % 4 != 0) return std::nullopt;

        while (off < len)
        {
            lsa.attachedRouters.push_back(readU32(buf + off));
            off += 4;
        }

        return lsa;
    }
};
}

#endif // NETWORK_LSA_V3_HPP
