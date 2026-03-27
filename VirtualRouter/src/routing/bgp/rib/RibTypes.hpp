/**
 * @file RibTypes.hpp
 * @brief RIB type definitions: Adj-RIB-In, Adj-RIB-Out, Loc-RIB routes.
 */

#ifndef BGP_RIB_TYPES_HPP
#define BGP_RIB_TYPES_HPP

#include <cstdint>
#include <vector>
#include <chrono>
#include <limits>
#include <unordered_map>

#include <IPAddress.h>

#include "bgp/attributes/AttributeTypes.hpp"
#include "bgp/attributes/AttributeManager.hpp"

namespace routing::bgp
{
class NeighborAf;

/**
 * @brief A single entry in a peer's Outbound Route Filter (ORF) prefix list.
 * @ingroup BGP_RIB
 *
 * Received from a peer via the ORF capability (RFC 5292). Each entry
 * describes one permitted or denied prefix range that the peer wants
 * us to apply when building Adj-RIB-Out for that peer.
 */
struct OrfPrefixEntry
{
    uint8_t  action;    ///< BGP_ORF_ACTION_* — ADD, REMOVE, or REMOVE_ALL.
    uint8_t  match;     ///< BGP_ORF_MATCH_* — PERMIT or DENY.
    uint32_t sequence;  ///< Sequence number for ordered prefix-list evaluation.
    uint8_t  minLen;    ///< Minimum prefix length to match (0 = use prefix length exactly).
    uint8_t  maxLen;    ///< Maximum prefix length to match (0 = use prefix length exactly).
    types::IPPrefix prefix;
};

/**
 * @brief An NLRI paired with an ADD-PATH path identifier.
 * @ingroup BGP_RIB
 *
 * When ADD-PATH is negotiated the same NLRI can be received from multiple
 * paths. The (nlri, pathId) pair is the unique key in Adj-RIB-In and
 * Adj-RIB-Out tables. For non-ADD-PATH sessions pathId is always 0.
 *
 * @tparam N  Prefix type (e.g. types::IPv4Prefix). Must be equality-comparable
 *             and hashable.
 */
template <typename N>
struct NlriPath
{
    N nlri;
    uint32_t pathId = 0; ///< ADD-PATH identifier; 0 for non-ADD-PATH peers.

    bool operator==(const NlriPath& o) const noexcept { return nlri == o.nlri && pathId == o.pathId; }
};

/**
 * @brief Hash functor for NlriPath<N> used in unordered containers.
 * @ingroup BGP_RIB
 *
 * Combines the hash of the NLRI with the path ID using a Boost-style
 * hash_combine to avoid collisions between (nlri=A, pathId=1) and
 * (nlri=B, pathId=0) pairs.
 *
 * @tparam N  Prefix type. Must be hashable via std::hash<N>.
 */
template <typename N>
struct NlriPathHash
{
    size_t operator()(const NlriPath<N>& k) const noexcept
    {
        size_t h = std::hash<N>{}(k.nlri);
        h ^= std::hash<uint32_t>{}(k.pathId) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/**
 * @brief Staging structure for building a single BGP UPDATE message.
 * @ingroup BGP_RIB
 *
 * Aggregates routes to be withdrawn and routes to be announced before
 * handing them to BgpTx. Announcements are grouped by path attributes so
 * that NLRIs sharing identical attributes can be packed into one UPDATE.
 *
 * @tparam N  Prefix type for this address family.
 */
template <typename N>
struct BuildUpdate
{
    /**
     * @brief A set of NLRIs that share the same egress path attributes.
     * @ingroup BGP_RIB
     *
     * BgpTx serializes each Announcement as one UPDATE message (or
     * multiple if the message would exceed the maximum size).
     */
    struct Announcement
    {
        PathAttribute attrs;             ///< Egress path attributes after outbound policy.
        std::vector<NlriPath<N>> nlri;   ///< Prefixes that carry these attributes.
    };

    std::vector<NlriPath<N>> withdrawn;       ///< Prefixes to withdraw from the peer.
    std::vector<Announcement> announcements;  ///< Prefixes to announce, grouped by attributes.
};

/**
 * @brief Parsed content of a received BGP UPDATE message for one AFI/SAFI.
 * @ingroup BGP_RIB
 *
 * Produced by BgpRx after decoding the wire-format UPDATE. The optional
 * `attrs` field is absent when the UPDATE carries only withdrawals.
 *
 * @tparam N  Prefix type for this address family.
 */
template <typename N>
struct ParsedUpdate
{
    std::vector<NlriPath<N>> withdrawn;     ///< Prefixes the peer is withdrawing.
    std::vector<NlriPath<N>> announcements; ///< Prefixes the peer is announcing.
    std::optional<PathAttribute> attrs;     ///< Path attributes; absent for withdraw-only UPDATEs.
};

/**
 * @brief RAII base for any BGP route object that holds an AttributeManager reference.
 * @ingroup BGP_RIB
 *
 * Manages the lifetime of a path-attribute reference inside AttributeManager.
 * Every route in Adj-RIB-In and Adj-RIB-Out derives from this class so that
 * path attributes are automatically released when the route is erased.
 *
 * ## Lifecycle & Ownership
 * Construction with `(AttributeManager&, uint32_t id)` assumes the caller
 * already holds one reference (from `acquire` or `retain`). Copy increments
 * the reference count; move transfers it without touching the count; destructor
 * decrements it.
 *
 * @warning Constructing with a pathId that has no live reference in the
 * AttributeManager will cause a double-release and corrupt the reference count.
 */
struct RouteBase
{
    RouteBase() = default;

    /**
     * @brief Constructs a RouteBase that takes ownership of one pre-counted reference.
     *
     * The caller must have already called AttributeManager::acquire or ::retain
     * for `id` before passing it here.
     *
     * @param mgr  AttributeManager that owns the path attributes.
     * @param id   Path attribute ID whose reference count the caller has already incremented.
     */
    RouteBase(AttributeManager& mgr, uint32_t id)
        : pathId(id), attrMgr(&mgr)
    {}

    RouteBase(const RouteBase& other)
        : pathId(other.pathId),
          attrMgr(other.attrMgr)
    {
        retainPathRef();
    }

    RouteBase(RouteBase&& other) noexcept
        : pathId(other.pathId),
          attrMgr(other.attrMgr)
    {
        other.pathId = std::nullopt;
        other.attrMgr = nullptr;
    }

    RouteBase& operator=(const RouteBase& other)
    {
        if (this == &other)
            return *this;

        releasePathRef();

        pathId = other.pathId;
        attrMgr = other.attrMgr;

        retainPathRef();
        return *this;
    }

    RouteBase& operator=(RouteBase&& other) noexcept
    {
        if (this == &other)
            return *this;

        releasePathRef();

        pathId = other.pathId;
        attrMgr = other.attrMgr;

        other.pathId = std::nullopt;
        other.attrMgr = nullptr;

        return *this;
    }

    /**
     * @brief Releases the path-attribute reference held by this route.
     */
    ~RouteBase()
    {
        releasePathRef();
    }

    /**
     * @brief Retrieves the path attributes from AttributeManager by pathId.
     *
     * @return A copy of the PathAttribute struct for this route.
     * @warning Behaviour is undefined if pathId is empty or attrMgr is null.
     */
    PathAttribute getPathAttributes() const
    {
        assert(attrMgr && pathId);
        return attrMgr->get(*pathId);
    }

    std::optional<uint32_t> pathId{}; ///< Index into AttributeManager; empty for locally-synthesised routes with no attributes yet.

protected:
    void retainPathRef()
    {
        if (attrMgr && pathId)
            attrMgr->retain(*pathId);
    }

    void releasePathRef()
    {
        if (attrMgr && pathId)
            attrMgr->release(*pathId);

        pathId = std::nullopt;
    }

private:
    AttributeManager* attrMgr = nullptr; ///< Non-owning pointer; lifetime must exceed this route.
};

/**
 * @brief Pre-policy snapshot of an inbound route for soft-reconfiguration.
 * @ingroup BGP_RIB
 *
 * Stored in Pre-Adj-RIB-In when `soft-reconfiguration inbound` is enabled.
 * Allows ingress policy to be re-applied without requiring a session reset,
 * because the original unfiltered attributes are preserved here.
 */
struct SoftPreEntry
{
    PathAttribute pa;      ///< Raw path attributes as received from the peer.
    uint32_t peerAs;       ///< Remote AS of the originating peer.
    bool ebgp;             ///< True if the route was received over an eBGP session.
    bool confedEbgp = false; ///< True if the session is a confederation eBGP session.
};

/**
 * @brief Common inbound route fields shared across all NLRI types.
 * @ingroup BGP_RIB
 *
 * Holds the session context and decision-process metadata for a route
 * received from a peer. The NLRI-typed subclass @ref InboundRoute adds
 * the prefix field.
 *
 * Locally-originated routes (network command, redistribution) are represented
 * with `sourceNeighbor == nullptr`, which triggers a default weight of 32768
 * per Cisco convention.
 *
 * @see InboundRoute, RouteBase
 */
struct InboundRouteBase : RouteBase
{
    /**
     * @brief Returns true if the route was locally originated (not received from a peer).
     */
    bool locallyOriginated() { return sourceNeighbor == nullptr; }

    /**
     * @brief Constructs a route shell associated with a neighbor (no path attributes yet).
     * @param nbr  Per-AF neighbor that sent this route; nullptr for locally-originated routes.
     */
    InboundRouteBase(NeighborAf* nbr)
        : sourceNeighbor(nbr), weigth(locallyOriginated() ? 32768 : 0) {}

    /**
     * @brief Constructs a fully populated inbound route with path attributes.
     * @param mgr  AttributeManager owning the path attributes.
     * @param id   Pre-counted attribute reference to take ownership of.
     * @param nbr  Per-AF neighbor; nullptr for locally-originated routes.
     */
    InboundRouteBase(AttributeManager& mgr, uint32_t id, NeighborAf* nbr = nullptr)
        : RouteBase(mgr, id), sourceNeighbor(nbr), weigth(locallyOriginated() ? 32768 : 0) {}

    NeighborAf* sourceNeighbor = nullptr;  ///< Peer that advertised this route; nullptr = locally originated.
    uint16_t weigth = 0;                   ///< Cisco-style weight; 32768 for locally-originated, 0 for received.
    uint32_t peerAs = 0;                   ///< Remote AS number of the originating peer.
    bool ebgp = true;                      ///< True if the route arrived over an eBGP session.
    bool confedEbgp = false;               ///< True if the session is a confederation eBGP peer.
    uint64_t igpCost = std::numeric_limits<uint64_t>::max(); ///< Resolved IGP metric to next-hop; max = unreachable.

    std::chrono::steady_clock::time_point receivedTime =
        std::chrono::steady_clock::now(); ///< Wall-clock time of receipt; used for oldest-route tiebreaker.

    bool operator==(const InboundRouteBase& other) const noexcept
    {
        return pathId == other.pathId && &sourceNeighbor == &other.sourceNeighbor;
    }
};

/**
 * @brief A single route in Adj-RIB-In, carrying a typed NLRI prefix.
 * @ingroup BGP_RIB
 *
 * Extends @ref InboundRouteBase with the prefix for one address family.
 * Non-copyable to prevent accidental reference-count duplication; move is
 * safe because RouteBase move leaves the source without a live reference.
 *
 * @tparam N  Prefix type (e.g. types::IPv4Prefix).
 *
 * @see InboundRouteBase, PerPeerInTable
 */
template <typename N>
struct InboundRoute : InboundRouteBase
{
    InboundRoute(NeighborAf* nbr) : InboundRouteBase(nbr) {}

    InboundRoute(AttributeManager& mgr, uint32_t id, N n, NeighborAf* nbr = nullptr)
        : InboundRouteBase(mgr, id, nbr), nlri(std::move(n)) {}

    N nlri; ///< The prefix this route describes.

    InboundRoute(const InboundRoute&) = delete;
    InboundRoute& operator=(const InboundRoute&) = delete;

    InboundRoute(InboundRoute&&) noexcept = default;
    InboundRoute& operator=(InboundRoute&&) noexcept = default;

    bool operator==(const InboundRoute& other) const noexcept
    {
        return nlri == other.nlri &&
               pathId == other.pathId &&
               &sourceNeighbor == &other.sourceNeighbor;
    }
};

/**
 * @brief Loc-RIB entry: the best route for a prefix plus ECMP and ADD-PATH peers.
 * @ingroup BGP_RIB
 *
 * Produced by DecisionEngine and stored in the Loc-RIB. The `route` field
 * points into Adj-RIB-In (must remain valid as long as this entry lives).
 * `multipaths` and `additionalPaths` are parallel references for ECMP and
 * ADD-PATH advertisement respectively.
 *
 * @tparam N  Prefix type.
 *
 * @warning The pointers in multipaths and additionalPaths reference entries
 * still held in Adj-RIB-In. They become dangling if those entries are erased
 * without updating or removing this LocalRoute first.
 */
template <typename N>
struct LocalRoute
{
    InboundRoute<N>& route;                           ///< Best-path winner; non-owning reference into Adj-RIB-In.
    std::vector<InboundRoute<N>*> multipaths;         ///< Equal-cost ECMP paths (excludes best route).
    std::vector<InboundRoute<N>*> additionalPaths;    ///< ADD-PATH advertisement pool, ranked by preference (excludes best and multipaths).
};

/**
 * @brief A single route in Adj-RIB-Out, carrying egress path attributes.
 * @ingroup BGP_RIB
 *
 * Represents what has already been sent (or is about to be sent) to a
 * specific peer after outbound policy. The pathId here refers to an egress
 * attribute set in AttributeManager, separate from the inbound one.
 *
 * @tparam N  Prefix type.
 */
template <typename N>
struct OutboundRoute : RouteBase
{
    OutboundRoute() = default;

    OutboundRoute(AttributeManager& mgr, uint32_t id, N n)
        : RouteBase(mgr, id), nlri(std::move(n)) {}

    N nlri{};

    bool operator==(const OutboundRoute& other) const noexcept
    {
        return nlri == other.nlri &&
               pathId == other.pathId;
    }
};

// PRE-POLICY ADJ-RIB-IN

/// @brief Pre-policy routes per peer, keyed by (NLRI, ADD-PATH ID). Used for soft-reconfiguration.
/// @tparam N  Prefix type.
template <typename N>
using PrePerPeerInTable = std::unordered_map<NlriPath<N>, SoftPreEntry, NlriPathHash<N>>;

// ADJ-RIB-IN

/// @brief Post-policy Adj-RIB-In for one peer, keyed by (NLRI, ADD-PATH ID).
/// @tparam N  Prefix type.
template <typename N>
using PerPeerInTable = std::unordered_map<NlriPath<N>, InboundRoute<N>, NlriPathHash<N>>;

// ADJ-RIB-OUT

/// @brief Adj-RIB-Out for one peer, keyed by NLRI; value is (ADD-PATH ID, outbound route).
/// A multimap is used because ADD-PATH allows multiple entries per NLRI.
/// @tparam N  Prefix type.
template <typename N>
using PerPeerOutTable = std::unordered_multimap<N, std::pair<uint32_t, OutboundRoute<N>>>;

/// @brief Per-peer Pre-Adj-RIB-In table indexed by peer Router-ID.
/// @tparam N  Prefix type.
template <typename N>
using PreAdjRibInTable = std::unordered_map<uint32_t, PrePerPeerInTable<N>>;

/// @brief Full Adj-RIB-In table indexed by peer Router-ID.
/// @tparam N  Prefix type.
template <typename N>
using AdjRibInTable = std::unordered_map<uint32_t, PerPeerInTable<N>>;

/// @brief Full Adj-RIB-Out table indexed by peer Router-ID.
/// @tparam N  Prefix type.
template <typename N>
using AdjRibOutTable = std::unordered_map<uint32_t, PerPeerOutTable<N>>;

/// @brief Loc-RIB table mapping each NLRI to its best LocalRoute.
/// @tparam N  Prefix type.
template <typename N>
using LocRibTable = std::unordered_map<N, LocalRoute<N>>;

} // namespace routing::bgp

#endif // BGP_RIB_TYPES_HPP

