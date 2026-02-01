// ExternalLsaV2.hpp

#ifndef EXTERNAL_LSA_V2_HPP
#define EXTERNAL_LSA_V2_HPP

#include <cstdint>
#include <IPAddress.hpp>
#include <HeaderHelpers.hpp>
#include <optional>
#include <OspfFletcher.hpp>

namespace OSPF
{
struct ExternalLsaV2
{
    uint32_t networkMask;
    uint32_t metric;
    bool isType2;
    uint32_t forwardingAddress;
    uint32_t routeTag;
    
    static std::optional<ExternalLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 16) return std::nullopt;

        ExternalLsaV2 lsa;

        lsa.networkMask = readU32(buf);

        uint32_t metricWord = readU32(buf + 4);
        lsa.isType2 = (metricWord & 0x80000000) != 0;
        lsa.metric = metricWord & 0x7FFFFFFF;

        lsa.forwardingAddress = readU32(buf + 8);
        lsa.routeTag = readU32(buf + 12);

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 16) return false;

        writeU32(buf, networkMask);
        uint32_t metricWord = metric & 0x7FFFFFFF;
        if (isType2) metricWord |= 0x80000000;
        writeU32(buf + 4, metricWord);
        if (isType2) buf[4] = 0x80;
        writeU32(buf + 8, forwardingAddress);
        writeU32(buf + 12, routeTag);
        return true;
    }

    static constexpr uint16_t size()
    {
        return 16;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(networkMask);
        uint32_t metricWord = metric & 0x7FFFFFFF;
        if (isType2) metricWord |= 0x80000000;
        check.addU32(metricWord);
        check.addU32(forwardingAddress);
        check.addU32(routeTag);
    }
};
}

#endif
