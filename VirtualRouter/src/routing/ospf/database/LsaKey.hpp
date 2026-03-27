/**
 * @file LsaKey.hpp
 * @brief OSPF LSA identity types: partial advertiser key, full three-tuple key, and type-scoped key.
 */

#ifndef OSPF_LSA_KEY_HPP
#define OSPF_LSA_KEY_HPP

#include <cstdint>
#include <functional>

namespace routing::ospf
{
using RouterId      = uint32_t; ///< 32-bit OSPF Router ID (network-byte-order integer).
using AreaId        = uint32_t; ///< 32-bit OSPF Area ID.
using InstanceId    = uint32_t; ///< OSPFv3 instance identifier.
using LinkStateId   = uint32_t; ///< LSA Link-State ID field.

struct LsaKey;

/**
 * @brief Partial LSA identity covering only LSA type and advertising router.
 * @ingroup OSPF_DATABASE
 *
 * Used as a secondary-index key when iterating all LSAs originated by a
 * single router, regardless of Link-State ID.  The LSDB maintains an
 * `A_LSDB` map keyed on `LsaAdvKey` for this purpose.
 *
 * `operator==` is provided for both `LsaAdvKey` and `LsaKey` so that
 * partial-match lookups can be done without constructing a full key.
 *
 * @see LsaKey
 */
struct LsaAdvKey
{
    LsaAdvKey() = default;
    LsaAdvKey(uint16_t type, RouterId advRtr)
        : lsaType(type), advertisingRouter(advRtr) {}

    bool operator==(const LsaAdvKey& o) const noexcept
    {
        return lsaType == o.lsaType &&
               advertisingRouter == o.advertisingRouter;
    }

    inline bool operator==(const LsaKey& o) const noexcept;

    uint16_t lsaType{0};
    RouterId advertisingRouter;
};

/**
 * @brief Full three-tuple OSPF LSA identity: type, Link-State ID, and advertising router.
 * @ingroup OSPF_DATABASE
 *
 * Uniquely identifies one LSA instance within a flooding scope (area or AS).
 * This is the primary key used in `U_LSDB` and `O_LSDB` maps.
 *
 * Inherits from `LsaAdvKey` so that code holding an `LsaKey` can be passed
 * directly anywhere an `LsaAdvKey` reference is expected, enabling partial
 * comparisons and secondary-index lookups without conversion.
 *
 * `operator<` uses a lexicographic ordering over `(lsaType, advertisingRouter,
 * linkStateId)` so that `LsaKey` can be used as an ordered-map key in
 * `O_LSDB`, which benefits SPF graph traversal locality.
 *
 * @see LsaAdvKey
 */
struct LsaKey : LsaAdvKey
{
    LsaKey() = default;
    LsaKey(uint16_t type, uint32_t id, RouterId advRtr)
        : LsaAdvKey(type, advRtr), linkStateId(id) {}

    uint32_t linkStateId{0};

    bool operator==(const LsaAdvKey& o) const noexcept
    {
        return lsaType == o.lsaType &&
               advertisingRouter == o.advertisingRouter;
    }

    bool operator==(const LsaKey& o) const noexcept
    {
        return lsaType == o.lsaType &&
               linkStateId == o.linkStateId &&
               advertisingRouter == o.advertisingRouter;
    }

    bool operator<(const LsaKey& o) const noexcept
    {
        return std::tie(lsaType, advertisingRouter, linkStateId)
             < std::tie(o.lsaType, o.advertisingRouter, o.linkStateId);
    }
};

bool LsaAdvKey::operator==(const LsaKey& o) const noexcept
{
    return lsaType == o.lsaType &&
           advertisingRouter == o.advertisingRouter;
}

/**
 * @brief Secondary lookup key scoped to a single LSA type: Link-State ID and advertising router.
 * @ingroup OSPF_DATABASE
 *
 * Used as the inner key of `T_LSDB`, which groups records by LSA type in the
 * outermost map.  Because the type is already captured by the outer bucket,
 * only the Link-State ID and advertising router are needed to identify a record
 * within that bucket.
 */
struct LsaTypeKey
{
    LsaTypeKey() = default;
    LsaTypeKey(uint32_t id, RouterId advRtr)
        : linkStateId(id), advRouter(advRtr) {}
    LsaTypeKey(const LsaKey& other)
        : linkStateId(other.linkStateId), advRouter(other.advertisingRouter) {}

    uint32_t linkStateId{0};
    uint32_t advRouter{0};

    bool operator==(const LsaTypeKey& o) const noexcept
    {
        return linkStateId == o.linkStateId &&
               advRouter == o.advRouter;
    }
};
} // namespace routing::ospf

namespace std
{
template <>
struct hash<routing::ospf::LsaAdvKey>
{
    size_t operator()(const routing::ospf::LsaAdvKey& k) const noexcept
    {
        uint64_t x = 0;
        x ^= static_cast<uint64_t>(k.lsaType) << 32;
        x ^= static_cast<uint64_t>(k.advertisingRouter) << 1;
        return std::hash<uint64_t>{}(x);
    }
};

template <>
struct hash<routing::ospf::LsaKey>
{
    size_t operator()(const routing::ospf::LsaKey& k) const noexcept
    {
        uint64_t x = 0;
        x ^= static_cast<uint64_t>(k.lsaType) << 32;
        x ^= static_cast<uint64_t>(k.linkStateId);
        x ^= static_cast<uint64_t>(k.advertisingRouter) << 1;
        return std::hash<uint64_t>{}(x);
    }
};

template <>
struct hash<routing::ospf::LsaTypeKey>
{
    size_t operator()(const routing::ospf::LsaTypeKey& k) const noexcept
    {
        uint64_t x = 0;
        x ^= static_cast<uint64_t>(k.linkStateId) << 32;
        x ^= static_cast<uint64_t>(k.advRouter) << 1;
        return std::hash<uint64_t>{}(x);
    }
};
}

#endif // OSPF_LSA_KEY_HPP
