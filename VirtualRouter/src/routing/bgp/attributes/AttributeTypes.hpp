// AttributeTypes.hpp

#ifndef BGP_ATTRIBUTE_TYPES_HPP
#define BGP_ATTRIBUTE_TYPES_HPP

#include <cstdint>
#include <optional>
#include <vector>
#include <array>

#include <IPAddress.h>

#include "bgp/BgpTypes.hpp"

namespace routing::bgp
{
struct AsPathSegment
{
    uint8_t segmentType = 0;
    std::vector<uint32_t> asns;

    inline bool operator==(const AsPathSegment&) const = default;
};

struct UnknownAttribute
{
    uint8_t flags = 0;
    uint8_t type = 0;
    std::vector<uint8_t> value;

    inline bool operator==(const UnknownAttribute&) const = default;
};

struct Aggregator
{
    uint32_t asn = 0;
    types::IPAddress speaker;

    inline bool operator==(const Aggregator&) const = default;
};

struct MpReach
{
    AfiSafi family;
    types::IPAddress nextHop;
    std::optional<types::IPAddress> linkLocal;
};

struct MpUnreach
{
    AfiSafi family;
};

struct Path
{
    AfiSafi family;
    types::IPAddress nextHop;
    std::optional<types::IPAddress> linkLocal;
    std::optional<uint64_t> rd;

    inline bool operator==(const Path&) const = default;
};

struct Attributes
{
    std::optional<uint8_t> origin;
    std::vector<AsPathSegment> asPath;
    std::vector<AsPathSegment> as4Path;
    std::optional<uint32_t> localPref;
    bool atomicAggregate = false;

    std::optional<uint32_t> med;
    std::optional<Aggregator> asAggregator;
    std::optional<Aggregator> as4Aggregator;
    std::vector<uint32_t> communities;
    std::vector<uint64_t> extendedCommunities;
    std::vector<std::array<uint32_t, 3>> largeCommunities;

    std::optional<uint32_t> originatorId;
    std::vector<uint32_t> clusterList;

    std::optional<uint64_t> aigp;

    std::vector<UnknownAttribute> unknownTransitive;    

    size_t asPathLength() const noexcept
    {
        size_t total = 0;
        for (const auto& seg : asPath)
        {
            // AS_CONFED_SEQUENCE (3) and AS_CONFED_SET (4) are excluded from path
            if (seg.segmentType == BGP_AS_CONFED_SEQUENCE ||
                seg.segmentType == BGP_AS_CONFED_SET)
                continue;
            // AS_SET (type 1) counts as 1 regardless of size (RFC 4271 §9.1.2.2)
            if (seg.segmentType == BGP_AS_SET)
                total += seg.asns.empty() ? 0 : 1;
            else
                total += seg.asns.size();
        }
        return total;
    }

    uint32_t firstAs() const noexcept
    {
        for (const auto& seg : asPath)
            if (seg.segmentType == BGP_AS_SEQUENCE && !seg.asns.empty())
                return seg.asns.front();
        return 0;
    }

    Attributes() = default;

    Attributes(const Attributes&) = default;
    Attributes& operator=(const Attributes&) = default;
    Attributes(Attributes&&) noexcept = default;
    Attributes& operator=(Attributes&&) noexcept = default;

    inline bool operator==(const Attributes&) const = default;
};

struct PathAttribute
{
    Attributes attrs;
    Path path;
};
} // namespace routing

namespace std
{
template <>
struct hash<routing::bgp::Path>
{
    inline size_t operator()(const routing::bgp::Path& p) const noexcept
    {
        uint64_t h =  0x9e3779b97f4a7c15ULL;

        auto mix = [&](uint64_t v)
        {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };

        mix(static_cast<uint64_t>(p.family.afi));
        mix(static_cast<uint64_t>(p.family.safi));
        mix(std::hash<types::IPAddress>{}(p.nextHop));

        if (p.linkLocal)
            mix(std::hash<types::IPAddress>{}(*p.linkLocal));

        return static_cast<size_t>(h);
    }
};

template <>
struct hash<routing::bgp::Attributes>
{
    inline size_t operator()(const routing::bgp::Attributes& a) const noexcept
    {
        uint64_t h = 0x9e3779b97f4a7c15ULL;
        
        auto mix = [&](uint64_t v)
        {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };

        // AS PATH
        for (const auto& seg : a.asPath)
        {
            mix(seg.segmentType);
            
            for (uint32_t as : seg.asns)
                mix(as);
        }

        // COMMUNITIES
        for (uint32_t c : a.communities)
            mix(c);

        for (uint64_t c : a.extendedCommunities)
            mix(c);

        for (const auto& lc : a.largeCommunities)
        {
            mix(lc[0]);
            mix(lc[1]);
            mix(lc[2]);
        }

        // MED
        if (a.med)
            mix(*a.med);

        // LOCAL PREF
        if (a.localPref)
            mix(*a.localPref);

        // ORIGINATOR ID
        if (a.originatorId)
            mix(*a.originatorId);

        // CLUSTER LIST
        for (uint32_t c : a.clusterList)
            mix(c);

        // AGGREGATOR
        if (a.asAggregator)
        {
            mix(a.asAggregator->asn);
            mix(std::hash<types::IPAddress>{}(a.asAggregator->speaker));
        }

        // ORIGIN
        if (a.origin)
            mix(*a.origin);

        // ATOMIC AGGREGATE
        mix(a.atomicAggregate);

        // AIGP
        if (a.aigp)
            mix(*a.aigp);

        // UNKNOWN_TRANSITIVE
        for (const auto& u : a.unknownTransitive)
        {
            mix(u.flags);
            mix(u.type);

            for (uint8_t b : u.value)
                mix(b);
        }
        return static_cast<size_t>(h);
    }
};
}

#endif // BGP_ATTRIBUTE_TYPES_HPP

