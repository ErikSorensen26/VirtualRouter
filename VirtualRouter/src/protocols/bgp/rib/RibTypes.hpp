// RibTypes.hpp

#ifndef BGP_RIB_TYPES_HPP
#define BGP_RIB_TYPES_HPP

#include <cstdint>
#include <vector>
#include <chrono>
#include <limits>

#include <IPAddress.hpp>

#include "bgp/attributes/AttributeTypes.hpp"

namespace BGP
{
template <typename N>
struct BuildUpdate
{
    struct Announcement
    {
        PathAttribute attrs;
        std::vector<N> nlri;
    };

    std::vector<N> withdrawn;
    std::vector<Announcement> announcements;
};

template <typename N>
struct ParsedUpdate
{
    std::vector<N> withdrawn;
    std::vector<N> announcements;
    std::optional<PathAttribute> attrs;
};

struct RouteCanidateBase
{
    Path path;
    uint32_t attrId;
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

    bool operator==(const RouteCanidate<N>& other) const noexcept
    {
        return nlri == other.nlri &&
               path == other.path &&
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
