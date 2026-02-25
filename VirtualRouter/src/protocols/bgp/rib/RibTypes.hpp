// RibTypes.hpp

#ifndef BGP_RIB_TYPES_HPP
#define BGP_RIB_TYPES_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include <IPAddress.hpp>

#include "bgp/BgpTypes.hpp"

namespace BGP
{
struct AsPathSegment
{
    uint8_t segmentType = 0;
    std::vector<uint32_t> asns;
};

struct UnknownAttribute
{
    uint8_t flags = 0;
    uint8_t type = 0;
    std::vector<uint8_t> value;
};

struct Aggregator
{
    uint32_t asn = 0;
    IPAddress speaker;
};

template <typename N>
struct MpReach
{
    AfiSafi family;
    IPAddress nextHop;
    std::vector<N> nlri;
};

template <typename N>
struct MpUnreach
{
    AfiSafi family;
    std::vector<N> withdrawl;
};

template <typename N>
struct PathAttribute
{
    std::optional<uint8_t> origin;
    std::vector<AsPathSegment> asPath;
    std::optional<IPAddress> nextHop;
    std::optional<uint32_t> localPref;
    std::optional<uint32_t> med;
    std::vector<uint32_t> communities;
    bool atomicAggregate = false;
    std::optional<Aggregator> aggregator;
    std::vector<AsPathSegment> as4Path;
    std::optional<Aggregator> as4Aggregator;
    std::optional<MpReach<N>> mpReach;
    std::optional<MpUnreach<N>> mpUnreach;
    std::vector<UnknownAttribute> unknownTransitive;

    size_t asPathLength() const noexcept
    {
        size_t total = 0;
        for (const auto& seg : asPath)
            total += seg.asns.size();
        return total;
    }
};

template <typename N>
struct ParsedUpdate
{
    std::vector<N> withdrawn;
    PathAttribute<N> attribute;
    std::vector<N> nlri;
};

template <typename N>
struct RouteCanidate
{
    AfiSafi family;
    N nlri;
    PathAttribute<N> attributes;

    IPAddress nextHop;
    uint32_t peerAs = 0;
    uint32_t neighborRouterId = 0;
    IPAddress neighborAddress;
    bool ebgp = true;

    uint64_t igpCost = std::numeric_limits<uint64_t>::max();
    std::chrono::steady_clock::time_point receivedTime = std::chrono::steady_clock::now();
};

template <typename N>
struct RouteCanidateKey
{
    AfiSafi family;
    N nlri;

    bool operator==(const RouteCanidateKey<N>& other) const noexcept
    {
        return family == other.family && nlri == other.nlri;
    }
};
}

namespace std
{
template <typename N>
struct hash<BGP::RouteCanidateKey<N>>
{
    size_t operator()(const BGP::RouteCanidateKey<N>& key) const noexcept
    {
        std::size_t h1 = std::hash<BGP::AfiSafi>{}(key.family);
        std::size_t h2 = std::hash<N>{}(key.nlri);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
}

#endif // BGP_RIB_TYPES_HPP
