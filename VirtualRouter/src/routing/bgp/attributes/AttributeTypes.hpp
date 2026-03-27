/**
 * @file AttributeTypes.hpp
 * @brief BGP path attribute types used across the RIB, decision engine, and transport layers.
 */

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
/**
 * @brief One segment of a BGP AS_PATH attribute.
 * @ingroup BGP_ATTRIBUTES
 *
 * A BGP AS_PATH is a sequence of segments, each of which is either an
 * AS_SEQUENCE, AS_SET, AS_CONFED_SEQUENCE, or AS_CONFED_SET (RFC 4271 §4.3).
 * `segmentType` holds the raw wire value so the receiver can distinguish
 * confederation segments without an extra enum conversion.
 */
struct AsPathSegment
{
    uint8_t segmentType = 0;        ///< Wire type code: BGP_AS_SEQUENCE (2), BGP_AS_SET (1), BGP_AS_CONFED_* (3/4).
    std::vector<uint32_t> asns;     ///< Ordered list of 4-byte AS numbers in this segment.

    inline bool operator==(const AsPathSegment&) const = default;
};

/**
 * @brief A BGP path attribute whose type code is not recognised by this implementation.
 * @ingroup BGP_ATTRIBUTES
 *
 * RFC 4271 §5 requires that unrecognised attributes with the Transitive flag
 * set are passed through to peers unchanged.  This struct preserves the raw
 * flags, type code, and value bytes so the transmission layer can re-encode
 * them without loss.
 */
struct UnknownAttribute
{
    uint8_t flags = 0;              ///< Wire flags byte (Optional, Transitive, Partial, Extended-Length).
    uint8_t type = 0;               ///< Attribute type code not handled by any known parser.
    std::vector<uint8_t> value;     ///< Raw attribute value bytes, excluding the type/flags/length header.

    inline bool operator==(const UnknownAttribute&) const = default;
};

/**
 * @brief AGGREGATOR / AS4_AGGREGATOR attribute value.
 * @ingroup BGP_ATTRIBUTES
 *
 * Identifies the last AS that performed route aggregation and the BGP
 * speaker within that AS.  Both the 2-byte AGGREGATOR and 4-byte
 * AS4_AGGREGATOR attributes map to this type; callers that need to
 * reconcile them should prefer `as4Aggregator` when present (RFC 4893).
 */
struct Aggregator
{
    uint32_t asn = 0;               ///< AS number of the aggregating speaker.
    types::IPAddress speaker;       ///< BGP Router ID of the aggregating speaker.

    inline bool operator==(const Aggregator&) const = default;
};

/**
 * @brief MP_REACH_NLRI attribute: next-hop and address-family information for a reachability announcement.
 * @ingroup BGP_ATTRIBUTES
 *
 * Carries the AFI/SAFI, global next-hop, and optionally a link-local
 * IPv6 next-hop as defined in RFC 4760 §3 and RFC 8950 §3.
 */
struct MpReach
{
    AfiSafi family;                             ///< Address family this reachability entry belongs to.
    types::IPAddress nextHop;                   ///< Global (or IPv4-mapped) next-hop address.
    std::optional<types::IPAddress> linkLocal;  ///< Link-local IPv6 next-hop; present only for IPv6 peers (RFC 8950).
};

/**
 * @brief MP_UNREACH_NLRI attribute: address-family marker for a withdrawal.
 * @ingroup BGP_ATTRIBUTES
 *
 * The NLRI prefixes being withdrawn are decoded separately; this struct
 * carries only the AFI/SAFI context needed to route them to the correct
 * Adj-RIB-In instance.
 */
struct MpUnreach
{
    AfiSafi family; ///< Address family of the prefixes being withdrawn.
};

/**
 * @brief Combined next-hop key that uniquely identifies the forwarding path for a route.
 * @ingroup BGP_ATTRIBUTES
 *
 * `Path` is used as a hash-map key in the AttributeManager so that routes
 * sharing an identical next-hop can share a single retained path attribute
 * block.  The `rd` field carries the Route Distinguisher for VPN routes;
 * it is absent for plain unicast.
 */
struct Path
{
    AfiSafi family;                             ///< Address family this path entry belongs to.
    types::IPAddress nextHop;                   ///< Global next-hop address.
    std::optional<types::IPAddress> linkLocal;  ///< Optional link-local IPv6 next-hop.
    std::optional<uint64_t> rd;                 ///< Route Distinguisher for L3VPN routes; absent for unicast.

    inline bool operator==(const Path&) const = default;
};

/**
 * @brief Full set of BGP path attributes decoded from a single UPDATE message.
 * @ingroup BGP_ATTRIBUTES
 *
 * Each field corresponds to one optional or well-known BGP attribute.
 * Fields absent in the wire encoding remain at their zero/empty defaults.
 * `as4Path` and `as4Aggregator` are populated from the AS4_PATH /
 * AS4_AGGREGATOR optional transitive attributes and are reconciled with
 * the 2-byte fields by the receiver per RFC 4893 §4.2.3.
 *
 * ## Architectural Role
 * `Attributes` is the in-memory representation stored in Adj-RIB-In entries
 * and passed through the decision engine.  It is also the type over which
 * `std::hash<Attributes>` is defined so that identical attribute sets can be
 * deduplicated in the `AttributeManager`.
 *
 * @see PathAttribute
 * @see AttributeManager
 */
struct Attributes
{
    std::optional<uint8_t> origin;              ///< ORIGIN attribute (IGP=0, EGP=1, INCOMPLETE=2).
    std::vector<AsPathSegment> asPath;          ///< Decoded AS_PATH segments.
    std::vector<AsPathSegment> as4Path;         ///< AS4_PATH segments for 4-byte AS reconciliation (RFC 4893).
    std::optional<uint32_t> localPref;          ///< LOCAL_PREF; meaningful only within an AS.
    bool atomicAggregate = false;               ///< True when the ATOMIC_AGGREGATE attribute is present.

    std::optional<uint32_t> med;                ///< MULTI_EXIT_DISC; used to discriminate among multiple exit points.
    std::optional<Aggregator> asAggregator;     ///< 2-byte AGGREGATOR attribute.
    std::optional<Aggregator> as4Aggregator;    ///< 4-byte AS4_AGGREGATOR attribute; overrides asAggregator when present.
    std::vector<uint32_t> communities;          ///< COMMUNITY attribute values (RFC 1997).
    std::vector<uint64_t> extendedCommunities;  ///< EXTENDED_COMMUNITIES values (RFC 4360), packed as 8-byte integers.
    std::vector<std::array<uint32_t, 3>> largeCommunities; ///< LARGE_COMMUNITY triplets {global, local1, local2} (RFC 8092).

    std::optional<uint32_t> originatorId;       ///< ORIGINATOR_ID set by a route reflector (RFC 4456).
    std::vector<uint32_t> clusterList;          ///< CLUSTER_LIST added by route reflectors (RFC 4456).

    std::optional<uint64_t> aigp;               ///< Accumulated IGP metric (RFC 7311); used in best-path step after LOCAL_PREF.

    std::vector<UnknownAttribute> unknownTransitive; ///< Unrecognised transitive attributes preserved for pass-through (RFC 4271 §5).

    /**
     * @brief Returns the effective AS_PATH hop count for best-path comparison.
     *
     * Implements RFC 4271 §9.1.2.2: AS_SEQUENCE segments contribute their
     * full ASN count, AS_SET counts as exactly 1 regardless of size, and
     * confederation segments (AS_CONFED_SEQUENCE / AS_CONFED_SET) are
     * excluded entirely.
     *
     * @return Number of hops for path-length comparison.
     */
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

    /**
     * @brief Returns the first AS number in the AS_PATH, or 0 if the path is empty.
     *
     * Scans the path for the first AS_SEQUENCE segment with at least one ASN.
     * Used by the `as-path access-list` peer-AS check and by `enforce-first-as`.
     *
     * @return First AS number, or 0 if no AS_SEQUENCE segment exists.
     */
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

/**
 * @brief Pairs a full @ref Attributes set with the resolved @ref Path (next-hop + family).
 * @ingroup BGP_ATTRIBUTES
 *
 * Stored in Adj-RIB-In entries after UPDATE parsing.  The split between
 * `Attributes` and `Path` allows the AttributeManager to deduplicate
 * attribute blocks keyed solely on `Path` while keeping the full attribute
 * set available for policy evaluation.
 *
 * @see Attributes
 * @see Path
 */
struct PathAttribute
{
    Attributes attrs;   ///< Full decoded path attributes for this route.
    Path path;          ///< Resolved next-hop and address-family context.
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

