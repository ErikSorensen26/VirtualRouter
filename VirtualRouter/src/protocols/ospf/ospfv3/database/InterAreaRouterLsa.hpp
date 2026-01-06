// InterAreaRouterLsa.hpp

#ifndef INTER_AREA_ROUTER_LSA_HPP
#define INTER_AREA_ROUTER_LSA_HPP

#include <cstdint>
#include <HeaderHelpers.hpp>
#include <optional>

namespace OSPF
{
struct InterAreaRouterLsa
{
    uint32_t options;
    uint32_t metric;
    uint32_t destinationRouterId;

    static std::optional<InterAreaRouterLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 12) return std::nullopt;

        InterAreaRouterLsa lsa;

        if (buf[0] != 0) return std::nullopt;
        lsa.options = readU24(buf + 1);

        if (buf[4] != 0) return std::nullopt;
        lsa.metric = readU24(buf + 1);

        lsa.destinationRouterId = readU32(buf + 8);

        return lsa;
    }
};
}

#endif // INTER_AREA_ROUTER_LSA_HPP
