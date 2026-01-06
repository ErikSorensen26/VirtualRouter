// ExternalLsaV2.hpp

#ifndef EXTERNAL_LSA_V2_HPP
#define EXTERNAL_LSA_V2_HPP

#include <cstdint>
#include <IPAddress.hpp>
#include <HeaderHelpers.hpp>
#include <optional>

namespace OSPF
{
struct ExternalLsaV2
{
    uint32_t networkMask;
    uint32_t metric;
    bool isType2;
    uint32_t forwardingAddress;
    uint32_t routerTag;
    
    static std::optional<ExternalLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 16) return std::nullopt;

        ExternalLsaV2 lsa;

        lsa.networkMask = readU32(buf);

        uint32_t metricWord = readU32(buf + 4);
        lsa.isType2 = (metricWord & 0x80000000) != 0;
        lsa.metric = metricWord & 0x00FFFFFF;

        lsa.forwardingAddress = readU32(buf + 8);
        lsa.routerTag = readU32(buf + 12);

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 16) return false;

        writeU32(buf, networkMask);
        writeU32(buf + 4, metric);
        if (isType2) buf[4] = 0x80;
        writeU32(buf + 8, forwardingAddress);
        writeU32(buf + 12, routerTag);
        return true;
    }
};
}

#endif
