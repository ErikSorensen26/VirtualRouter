// SummaryNetworkLsa.hpp

#ifndef SUMMARY_NETWORK_LSA_HPP
#define SUMMARY_NETWORK_LSA_HPP

#include <cstdint>
#include <HeaderHelpers.hpp>
#include <optional>
#include <OspfFletcher.hpp>

namespace OSPF
{
struct SummaryNetworkLsa
{
    uint32_t networkMask;
    uint32_t metric;

    static std::optional<SummaryNetworkLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 8) return std::nullopt;

        SummaryNetworkLsa lsa;
        lsa.networkMask = readU32(buf);
        uint32_t metricWord = readU32(buf + 4);
        lsa.metric = metricWord & 0x00FFFFFF;

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 8) return false;

        writeU32(buf, networkMask);
        writeU32(buf + 4, metric);
        return true;
    }

    static constexpr size_t size()
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
}

#endif // SUMMARY_NETWORK_LSA_HPP
