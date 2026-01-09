// SummaryRouterLsa

#ifndef SUMMARY_ROUTER_LSA_HPP
#define SUMMARY_ROUTER_LSA_HPP

#include <cstdint>
#include <HeaderHelpers.hpp>
#include <optional>
#include <OspfFletcher.hpp>

namespace OSPF
{
struct SummaryRouterLsa
{
    uint32_t metric;

    static std::optional<SummaryRouterLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 8) return std::nullopt;
        
        if (readU32(buf) != 0) return std::nullopt;

        SummaryRouterLsa lsa;

        uint32_t metricWord = readU32(buf + 4);
        lsa.metric = metricWord & 0x00FFFFFF;

        return lsa;
    }

    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 8) return false;

        writeU32(buf, 0);
        writeU32(buf + 4, metric);

        return true;
    }

    static constexpr size_t size()
    {
        return 8;
    }

    void appendChecksum(ChecksumFletcher& check) const
    {
        uint32_t metricWord = metric;
        metricWord |= 0x00FFFFFF;
        check.addU32(metricWord);
    }

    bool operator==(const SummaryRouterLsa& rhs) const
    {
        return metric == rhs.metric;
    }
};
}

#endif // SUMMARY_ROUTER_LSA_HPP
