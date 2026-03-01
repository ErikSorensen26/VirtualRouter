// RibTypes.hpp

#ifndef BGP_RIB_TYPES_HPP
#define BGP_RIB_TYPES_HPP

#include <cstdint>
#include <optional>
#include <vector>
#include <chrono>
#include <limits>

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
    std::vector<N> withdrawn;
};

struct PathAttributeBase
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
struct PathAttribute : PathAttributeBase
{
    std::optional<MpReach<N>> mpReach;
    std::optional<MpUnreach<N>> mpUnreach;
};

template <typename N>
struct ParsedUpdate
{
    std::vector<N> withdrawn;
    std::vector<std::pair<N, PathAttribute<N>>> announced;
};

struct RouteCanidateBase
{
    IPAddress nextHop;
    uint32_t peerAs = 0;
    uint32_t neighborRouterId = 0;
    IPAddress neighborAddress;
    bool ebgp = true;

    uint64_t igpCost = std::numeric_limits<uint64_t>::max();
    std::chrono::steady_clock::time_point receivedTime = std::chrono::steady_clock::now();
};

template <typename N>
struct RouteCanidate : RouteCanidateBase
{
    N nlri;
    PathAttribute<N> attributes;

    bool operator==(const RouteCanidate<N>& other) const noexcept
    {
        return nlri == other.nlri &&
               nextHop == other.nextHop &&
               peerAs == other.peerAs &&
               neighborRouterId == other.neighborRouterId &&
               neighborAddress == other.neighborAddress;
    }
};

template <typename N>
using PerPeerAdjTable = std::unordered_map<N, RouteCanidate<N>>;

template <typename N>
using AdjRibInTable = std::unordered_map<uint32_t, PerPeerAdjTable<N>>;

template <typename N>
using AdjRibOutTable = std::unordered_map<uint32_t, PerPeerAdjTable<N>>;

template <typename N>
using LocRibTable = std::unordered_map<N, RouteCanidate<N>>;
}

#endif // BGP_RIB_TYPES_HPP
