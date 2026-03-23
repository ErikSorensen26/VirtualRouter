// SummaryNetworkLsa.hpp

#ifndef SUMMARY_NETWORK_LSA_HPP
#define SUMMARY_NETWORK_LSA_HPP

#include <cstdint>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
struct SummaryNetworkLsa
{
    uint32_t networkMask;
    uint32_t metric;

    static std::optional<SummaryNetworkLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 8) return std::nullopt;

        SummaryNetworkLsa lsa;
        lsa.networkMask = utils::readU32(buf);
        uint32_t metricWord = utils::readU32(buf + 4);
        lsa.metric = metricWord & 0x00FFFFFF;

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 8) return false;

        utils::writeU32(buf, networkMask);
        utils::writeU32(buf + 4, metric);
        return true;
    }

    static constexpr uint16_t size()
    {
        return 8;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(networkMask);
        check.addU32(metric);
    }

    bool operator==(const SummaryNetworkLsa& rhs) const
    {
        return networkMask == rhs.networkMask && metric == rhs.metric;
    }
};
} // namespace routing

#endif // SUMMARY_NETWORK_LSA_HPP

