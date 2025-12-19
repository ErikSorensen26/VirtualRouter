// LSDB.h

#ifndef OSPF_LSDB_H
#define OSPF_LSDB_H

#include <cstdint>
#include <cstddef>
#include <variant>
#include <unordered_map>
#include <type_traits>

#include <memory_resource>
#include <vector>

#include <IPAddress.hpp>
#include "LsaKey.hpp"

namespace OSPF
{
// Set to 0 if you do not want std::pmr containers in LSDB storage
#ifndef OSPF_LSDB_USE_PMR
#define OSPF_LSDB_USE_PMR 1
#endif

#if OSPF_LSDB_USE_PMR
template <typename T> using Vec = std::pmr::vector<T>;
using ByteVec = std::pmr::vector<std::byte>;
template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using UMap = std::pmr::unordered_map<K, V, H, E>;
#else
template <typename T> using Vec = std::vector<T>;
using ByteVec = std::vector<std::byte>;
template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using UMap = std::unordered_map<K, V, H, E>;
#endif

// LSDB-stored header metadata (wire-derived)
struct LsaHeader final
{
    uint32_t sequence{0};
    uint16_t checksum{0};
    uint16_t length{0};
    uint16_t age{0};
    uint16_t pad{0};
};
static_assert(sizeof(LsaHeader) == 12, "Unexpected LsaHeader size");

// Compact per-record flags (pack booleans into one byte)
enum class LsaRecordFlags : uint8_t
{
    NONE = 0,
    SELF_ORIGINATED = 1u << 0,
    CHECKSUM_VALID = 1u << 1,
};

constexpr inline LsaRecordFlags operator|(LsaRecordFlags a, LsaRecordFlags b) noexcept
{
    return static_cast<LsaRecordFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr inline LsaRecordFlags operator&(LsaRecordFlags a, LsaRecordFlags b) noexcept
{
    return static_cast<LsaRecordFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr inline LsaRecordFlags operator|=(LsaRecordFlags a, LsaRecordFlags b) noexcept
{
    a = (a | b);
    return a;
}
constexpr inline bool hasFlag(LsaRecordFlags v, LsaRecordFlags f) noexcept
{
    return (v & f) != LsaRecordFlags::NONE;
}

struct LsaTlvNode final
{
    uint32_t valueOffset{0};            ///< Into LsaTlvForest::blob.
    uint32_t valueLength{0};            ///< bytes.
    uint32_t firstChild{0xFFFFFFFFu};   ///< Index in nodes, or kInvalid.
    uint32_t nextSibling{0xFFFFFFFFu};  ///< Index in nodes, or kInvalid.
    uint16_t type{0};
    uint16_t reserved{0};
};
static_assert(sizeof(LsaTlvNode) == 20, "Unexpected LsaTlvNode size");

struct LsaTlvForest final
{
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

    ByteVec blob;
    Vec<LsaTlvNode> nodes;

#if OSPF_LSDB_USE_PMR
    explicit LsaTlvForest(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : blob(mr), nodes(mr)
    {
        nodes.push_back(LsaTlvNode{});
    }
#else
    LsaTlvForest()
    {
        nodes.push_back(LsaTlvForest{});
    }
#endif
};

// LSA bodies (LSA semantics only; no packet structs)
struct RouterLink final
{
    uint32_t interfaceId{0};
    uint32_t neighborInterfaceId{0};
    RouterId neighborRouterId{0};
    uint16_t metric{0};
    uint8_t linkType{0};
    uint8_t reserved{0};
};

struct RouterLsa final
{
    uint32_t options{0};
    uint8_t flags{0};
    uint8_t pad[3]{};

    Vec<RouterLink> links;

#if OSPF_LSDB_USE_PMR
    explicit RouterLsa(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : links(mr) {}
#endif
};

struct NetworkLsa final
{
    uint32_t options{0};
    Vec<RouterId> attachedRouters;

#if OSPF_LSDB_USE_PMR
    explicit NetworkLsa(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : attachedRouters(mr) {}
#endif
};

struct InterAreaPrefixLsa final
{
    uint32_t metric{0};
    uint8_t prefixOptions{0};
    uint8_t pad[3]{};
    IPPrefix prefix{};
};

struct InterAreaRouterLsa final
{
    uint32_t options{0};
    uint32_t metric{0};
    RouterId destinationRouterId{};
};

struct ExternalLsa final
{
    uint32_t metric{0};
    uint32_t routerTag{0};

    uint8_t flags{0};
    uint8_t prefixOptions{0};
    uint16_t pad{0};
    
    IPPrefix prefix{};
    IPAddress forwardingAddress{};
};

struct LinkLsa final
{
    uint32_t options{0};
    uint8_t priority{0};
    uint8_t pad[3]{};

    IPAddress linkLocalAddress{};
    Vec<IPAddress> prefixes;

#if OSPF_LSDB_USE_PMR
    explicit LinkLsa(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : prefixes(mr) {}
#endif
};

struct IntraAreaPrefixLsa final
{
    uint16_t referencesLsaType{0};
    uint16_t pad0{0};
    uint32_t referendesLinkStateId{0};
    RouterId referencedAdvRouter{};

    Vec<IPPrefix> prefixes;

#if OSPF_LSDB_USE_PMR
    explicit IntraAreaPrefixLsa(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : prefixes(mr) {}
#endif
};

struct TlvLsa final
{
    LsaTlvForest tlvs;

#if OSPF_LSDB_USE_PMR
    explicit TlvLsa(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : tlvs(mr) {}
#endif
};

using LsaBody = std::variant<
    std::monostate,
    RouterLsa,
    NetworkLsa,
    InterAreaPrefixLsa,
    InterAreaRouterLsa,
    ExternalLsa,
    LinkLsa,
    IntraAreaPrefixLsa,
    TlvLsa
>;

// One LSDB record.
struct LsaRecord final
{
    std::chrono::steady_clock::time_point installTime;
    std::chrono::steady_clock::time_point lastRefreshTime;

    LsaHeader header{};
    LsaBody body{};

    LsaRecordFlags flags{LsaRecordFlags::NONE};
    uint8_t pad[7]{};
};

using LSDB = UMap<LsaKey, LsaRecord>;

} // namespace OSPF

#endif // OSPF_LSDB_H
