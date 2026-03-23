// ExternalLsaV2.hpp

#ifndef EXTERNAL_LSA_V2_HPP
#define EXTERNAL_LSA_V2_HPP

#include <cstdint>
#include <IPAddress.h>
#include <optional>

#include "ospf/transmission/OspfFletcher.hpp"
#include "packet/HeaderHelpers.hpp"

namespace routing::ospf
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

        lsa.networkMask = utils::readU32(buf);

        uint32_t metricWord = utils::readU32(buf + 4);
        lsa.isType2 = (metricWord & 0x80000000) != 0;
        lsa.metric = metricWord & 0x7FFFFFFF;

        lsa.forwardingAddress = utils::readU32(buf + 8);
        lsa.routeTag = utils::readU32(buf + 12);

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 16) return false;

        utils::writeU32(buf, networkMask);
        uint32_t metricWord = metric & 0x7FFFFFFF;
        if (isType2) metricWord |= 0x80000000;
        utils::writeU32(buf + 4, metricWord);
        if (isType2) buf[4] = 0x80;
        utils::writeU32(buf + 8, forwardingAddress);
        utils::writeU32(buf + 12, routeTag);
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
} // namespace routing

#endif

