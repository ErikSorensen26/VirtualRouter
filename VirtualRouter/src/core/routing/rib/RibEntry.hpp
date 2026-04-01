/**
 * @file RibEntry.hpp
 * @brief Route entry types stored in the RIB and propagated to the FIB.
 */

#ifndef RIB_ENTRY_HPP
#define RIB_ENTRY_HPP

#include "RouteSource.hpp"
#include <IPAddress.h>
#include <optional>
#include "interface/configs/InterfaceType.hpp"

namespace core
{

/// Maximum number of equal-cost next-hops per route entry.
#define MAX_NEXTHOP 8

// PREFIX KEY / HASH

/**
 * @brief Composite key representing a network prefix and its length.
 * @tparam AddrType Unsigned integral address type.
 */
template <typename AddrType>
struct PrefixKey
{
    AddrType prefix; ///< Network address (host-bit masked).
    uint8_t  length; ///< Prefix length in bits.

    bool operator==(const PrefixKey& o) const noexcept
    {
        return prefix == o.prefix && length == o.length;
    }
};

/**
 * @brief Hash functor for `PrefixKey`, suitable for `std::unordered_map`.
 * @ingroup CORE_ROUTING_RIB
 * @tparam AddrType Unsigned integral address type.
 */
template <typename AddrType>
struct PrefixHash
{
    /**
     * @brief Compute a hash value for the given prefix key.
     * @ingroup CORE_ROUTING_RIB
     * @param k Prefix key to hash.
     * @return Hash value.
     */
    size_t operator()(const PrefixKey<AddrType>& k) const noexcept
    {
        uint64_t h1 = std::hash<AddrType>{}(k.prefix);
        uint64_t h2 = k.length;
        uint64_t hash = h1;
        hash ^= h2 + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return static_cast<size_t>(hash);
    }
};

// NEXT-HOP PATH

/**
 * @brief A single next-hop entry within a route, carrying an optional gateway
 *        address, an egress interface index, and an ECMP weight.
 * @tparam AddrType Unsigned integral address type.
 */
template <typename AddrType>
struct NextHopPath
{
    std::optional<AddrType> nextHop; ///< Gateway address; absent for connected routes.
    interface::InterfaceKey iface;   ///< Egress interface index.
    uint32_t weight;                 ///< Relative ECMP weight (1 = equal share).
};

// RIB ENTRY

/**
 * @brief A single route entry stored in the Routing Information Base.
 * @ingroup CORE_ROUTING_RIB
 *
 * `RibEntry` holds all data the RIB needs to select the best path and push
 * a forwarding entry to the FIB.  It supports up to `MAX_NEXTHOP`
 * equal-cost next-hops for ECMP.  The struct is copyable and copy-assignable
 * so that `RibBucket::selectBest()` can heap-allocate a snapshot for the FIB
 * without exposing a pointer into the (potentially reallocating) routes vector.
 *
 * ## Architectural Role
 * Produced by routing protocols (BGP, EIGRP, OSPF, …) and passed to
 * `RoutingTable::addRoute()`.  The RIB takes ownership of heap-allocated
 * entries; callers must allocate with `new` or pass by pointer as documented.
 *
 * ## Concurrency Model
 * RibEntry objects live in `RibBucket::routes` (control-plane only) or as
 * heap copies in `RibBucket::fibEntry` (read lock-free via RCU).  Do not
 * modify a `RibEntry` after it has been published to the FIB.
 *
 * @tparam AddrType Unsigned integral address type (`uint32_t` / `__uint128_t`).
 *
 * @see RibBucket
 * @see FibEntry
 */
template <typename AddrType>
struct RibEntry
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

    AddrType    prefix;        ///< Network-masked prefix address.
    uint8_t     length;        ///< Prefix length in bits.
    RouteSource source;        ///< Protocol that installed this route.
    uint64_t    processId;     ///< Protocol process instance identifier (e.g. AS number for BGP).
    uint8_t     adminDistance; ///< Administrative distance; lower wins inter-protocol.
    uint64_t    metric;        ///< Protocol-specific route metric; lower is preferred.
    uint32_t    tag = 0;       ///< Optional route tag (used by redistribution policy).
    void*       topInfo = nullptr; ///< Protocol-specific opaque pointer (e.g. BGP path attributes).

    NextHopPath<AddrType> nextHops[MAX_NEXTHOP]; ///< Array of ECMP next-hops.
    uint8_t               nextHopCount = 0;      ///< Number of valid entries in `nextHops`.

    /// @brief Default-construct an empty (no next-hops) entry.
    RibEntry() = default;

    /**
     * @brief Clear all next-hop entries, marking the route as unreachable.
     * @ingroup CORE_ROUTING_RIB
     */
    void clear() noexcept { nextHopCount = 0; }

    /**
     * @brief Return `true` if the entry has no next-hops.
     */
    bool empty() const noexcept { return nextHopCount == 0; }

    /**
     * @brief Add a gateway next-hop to this entry.
     *
     * If a slot already exists for the same interface without a gateway, the
     * gateway is filled in.  Duplicate (address, interface) pairs are ignored.
     *
     * @param nhAddr Gateway address.
     * @param iface  Egress interface index.
     * @param weight ECMP weight (default 1).
     * @return `true` if the next-hop was added or updated; `false` if the
     *         slot table is full or an identical entry already exists.
     */
    bool addNextHop(const AddrType nhAddr, interface::InterfaceKey iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
        {
            auto& nextHop = nextHops[i];
            if (!nextHop.nextHop.has_value() && nextHop.iface == iface)
            {
                nextHop.nextHop = nhAddr;
                return true;
            }
            else if (nextHop.nextHop.has_value() && nextHop.nextHop.value() == nhAddr && nextHop.iface == iface)
            {
                return false;
            }
        }

        nextHops[nextHopCount++] = { nhAddr, iface, weight };
        return true;
    }

    /**
     * @brief Add an interface-only (connected / unnumbered) next-hop.
     * @param iface  Egress interface index.
     * @param weight ECMP weight (default 1).
     * @return `true` if added; `false` if the slot table is full or the
     *         interface is already present.
     */
    bool addNextHopInterface(uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
            if (nextHops[i].iface == iface)
                return false;
        nextHops[nextHopCount++] = { std::nullopt, iface, weight };
        return true;
    }

    /**
     * @brief Copy constructor — copies only the active next-hop slots.
     * @param other Source entry.
     */
    RibEntry(const RibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          source(other.source),
          processId(other.processId),
          adminDistance(other.adminDistance),
          metric(other.metric),
          tag(other.tag),
          topInfo(other.topInfo),
          nextHopCount(other.nextHopCount)
    {
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];
    }

    /**
     * @brief Copy-assignment operator.
     * @param other Source entry.
     * @return Reference to `*this`.
     */
    RibEntry& operator=(const RibEntry<AddrType>& other) noexcept
    {
        if (this == &other)
            return *this;

        prefix = other.prefix;
        length = other.length;
        source = other.source;
        processId = other.processId;
        adminDistance = other.adminDistance;
        metric = other.metric;
        tag = other.tag;
        topInfo = other.topInfo;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }
};

// FIB ENTRY

/**
 * @brief Stripped-down forwarding entry pushed into the FIB after best-path
 *        selection.
 *
 * `FibEntry` retains only the forwarding-relevant fields from `RibEntry`
 * (prefix, length, next-hops) and discards control-plane metadata such as
 * admin distance, metric, and protocol info.  It can be constructed or
 * assigned from a `RibEntry`.
 *
 * ## Concurrency Model
 * Published to the FIB as a heap-allocated RCU object.  Do not modify after
 * publishing.
 *
 * @tparam AddrType Unsigned integral address type.
 *
 * @see RibEntry
 * @see Fib
 */
template <typename AddrType>
struct FibEntry
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

    AddrType prefix;              ///< Network-masked prefix address.
    uint8_t  length;              ///< Prefix length in bits.
    NextHopPath<AddrType> nextHops[MAX_NEXTHOP]; ///< Forwarding next-hops.
    uint8_t  nextHopCount = 0;   ///< Number of valid entries in `nextHops`.

    /// @brief Default-construct an empty entry.
    FibEntry() = default;

    /**
     * @brief Clear all next-hops.
     */
    void clear() noexcept { nextHopCount = 0; }

    /**
     * @brief Return `true` if no next-hops are present.
     */
    bool empty() const noexcept { return nextHopCount == 0; }

    /**
     * @brief Add a gateway next-hop.
     * @param nhAddr Gateway address.
     * @param iface  Egress interface index.
     * @param weight ECMP weight (default 1).
     * @return `true` if added or updated; `false` if full or duplicate.
     */
    bool addNextHop(const AddrType nhAddr, uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
        {
            auto& nextHop = nextHops[i];
            if (!nextHop.nextHop.has_value() && nextHop.iface == iface)
            {
                nextHop.nextHop = nhAddr;
                return true;
            }
            else if (nextHop.nextHop.has_value() && nextHop.nextHop.value() == nhAddr && nextHop.iface == iface)
            {
                return false;
            }
        }

        nextHops[nextHopCount++] = { nhAddr, iface, weight };
        return true;
    }

    /**
     * @brief Add an interface-only next-hop.
     * @param iface  Egress interface index.
     * @param weight ECMP weight (default 1).
     * @return `true` if added; `false` if full or duplicate.
     */
    bool addNextHopInterface(uint32_t iface, uint32_t weight = 1) noexcept
    {
        if (nextHopCount >= MAX_NEXTHOP)
            return false;

        for (uint8_t i = 0; i < nextHopCount; ++i)
            if (nextHops[i].iface == iface)
                return false;
        nextHops[nextHopCount++] = { std::nullopt, iface, weight };
        return true;
    }

    /**
     * @brief Copy constructor from another `FibEntry`.
     * @param other Source entry.
     */
    FibEntry(const FibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          nextHopCount(other.nextHopCount)
    {
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];
    }

    /**
     * @brief Construct a `FibEntry` from the forwarding-relevant fields of a
     *        `RibEntry`.
     * @param other Source RIB entry.
     */
    FibEntry(const RibEntry<AddrType>& other) noexcept
        : prefix(other.prefix),
          length(other.length),
          nextHopCount(other.nextHopCount)
    {
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];
    }

    /**
     * @brief Copy-assignment from another `FibEntry`.
     * @param other Source entry.
     * @return Reference to `*this`.
     */
    FibEntry& operator=(const FibEntry<AddrType>& other) noexcept
    {
        if (this == &other)
            return *this;

        prefix = other.prefix;
        length = other.length;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }

    /**
     * @brief Assign forwarding fields from a `RibEntry`.
     * @param other Source RIB entry.
     * @return Reference to `*this`.
     */
    FibEntry& operator=(const RibEntry<AddrType>& other) noexcept
    {
        prefix = other.prefix;
        length = other.length;

        nextHopCount = other.nextHopCount;
        for (uint8_t i = 0; i < other.nextHopCount; ++i)
            nextHops[i] = other.nextHops[i];

        return *this;
    }
};

} // namespace core

#endif // RIB_ENTRY_HPP
