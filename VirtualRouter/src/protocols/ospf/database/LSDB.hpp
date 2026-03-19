// LSDB.h

#ifndef OSPF_LSDB_H
#define OSPF_LSDB_H

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <memory_resource>
#include <vector>
#include <atomic>
#include <chrono>
#include <variant>

#include "packet/headers/embedded/ospf/Ospfv2LSAHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3LSAHeader.hpp"

#include "LsaKey.hpp"

#include "ospf/ospfv2/database/RouterLsaV2.hpp"
#include "ospf/ospfv2/database/NetworkLsaV2.hpp"
#include "ospf/ospfv2/database/SummaryNetworkLsa.hpp"
#include "ospf/ospfv2/database/SummaryRouterLsa.hpp"
#include "ospf/ospfv2/database/ExternalLsaV2.hpp"
#include "ospf/ospfv2/database/OpaqueLsaV2.hpp" // TODO

#include "ospf/ospfv3/database/RouterLsaV3.hpp"
#include "ospf/ospfv3/database/NetworkLsaV3.hpp"
#include "ospf/ospfv3/database/InterAreaPrefixLsa.hpp"
#include "ospf/ospfv3/database/InterAreaRouterLsa.hpp"
#include "ospf/ospfv3/database/ExternalLsaV3.hpp"
#include "ospf/ospfv3/database/LinkLsa.hpp"
#include "ospf/ospfv3/database/IntraAreaPrefixLsa.hpp"

#include "ospf/area/FloodTypes.hpp"

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
template <typename K, typename H = std::hash<K>, typename E = std::equal_to<K>>
using USet = std::pmr::unordered_set<K, H, E>;
template <typename K, typename V>
using OMap = std::pmr::map<K, V>;
#else
template <typename T> using Vec = std::vector<T>;
using ByteVec = std::vector<std::byte>;
template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using UMap = std::unordered_map<K, V, H, E>;
template <typename K, typename V>
using OMap = std::map<K, V>;
#endif

// LSDB-stored header metadata (wire-derived)
struct LsaHeader final
{
    uint32_t sequence{0};
    uint16_t checksum{0};
    uint16_t length{0};
    uint16_t age{0};
    uint8_t options{0};

    bool operator==(LsaHeader& rhs)
    {
        return sequence == rhs.sequence &&
               checksum == rhs.checksum &&
               length == rhs.length &&
               age == rhs.age &&
               options == rhs.options;
    }
};
static_assert(sizeof(LsaHeader) == 12, "Unexpected LsaHeader size");

// Compact per-record flags (pack booleans into one byte)
enum class LsaRecordFlags : uint8_t
{
    NONE = 0,
    SELF_ORIGINATED = 1u << 0,
    CHECKSUM_VALID = 1u << 1,
};

// Compact incoming lsa information
struct IncomingLsaContext final
{
    const LsaKey& key;
    LsaHeader& header;
    
    bool checksumValid{false};
    bool selfOriginatedKey{false};
    FloodInfo info{};

    uint32_t incomingInterface{0};
    uint32_t incomingNeighbor{0};
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
        : blob(mr), nodes(mr) { nodes.push_back(LsaTlvNode{}); }
#else
    LsaTlvForest()
    { nodes.push_back(LsaTlvForest{}); }
#endif
};


/*#if OSPF_LSDB_USE_PMR
    explicit NetworkLsa(std::pmr::memory_resource* mr = std::pmr::get_default_resource())
        : attachedRouters(mr) {}
#endif*/

using LsaBody = std::variant<
    std::monostate,
    RouterLsaV2,
    NetworkLsaV2,
    SummaryNetworkLsa,
    SummaryRouterLsa,
    ExternalLsaV2,
    //OpaqueLsaV2,
    RouterLsaV3,
    NetworkLsaV3,
    InterAreaPrefixLsa,
    InterAreaRouterLsa,
    ExternalLsaV3,
    LinkLsa,
    IntraAreaPrefixLsa
>;

// Policies
struct PolicyV2
{
    using RouterLsa = RouterLsaV2;
    using NetworkLsa = NetworkLsaV2;
    using InterNetworkLsa = SummaryNetworkLsa;
    using InterRouterLsa = SummaryRouterLsa;
    using ExternalLsa = ExternalLsaV2;

    static constexpr uint16_t RouterLsaType = OSPFV2_LSA_ROUTER;
    static constexpr uint16_t NetworkLsaType = OSPFV2_LSA_NETWORK;
    static constexpr uint16_t InterNetworkType = OSPFV2_LSA_SUM_NET;
    static constexpr uint16_t InterRouterType = OSPFV2_LSA_SUM_ASBR;
    static constexpr uint16_t ExternalType = OSPFV2_LSA_EXTERNAL;
    static constexpr uint16_t NssaType = OSPFV2_LSA_NSSA;
};

struct PolicyV3
{
    using RouterLsa = RouterLsaV3;
    using NetworkLsa = NetworkLsaV3;
    using InterNetworkLsa = InterAreaPrefixLsa;
    using InterRouterLsa = InterAreaRouterLsa;
    using ExternalLsa = ExternalLsaV3;

    static constexpr uint16_t RouterLsaType = OSPFV3_LSA_ROUTER;
    static constexpr uint16_t NetworkLsaType = OSPFV3_LSA_NETWORK;
    static constexpr uint16_t InterNetworkType = OSPFV3_LSA_INTER_AREA_PREFIX;
    static constexpr uint16_t InterRouterType = OSPFV3_LSA_INTER_AREA_ROUTER;
    static constexpr uint16_t ExternalType = OSPFV3_LSA_AS_EXTERNAL;
    static constexpr uint16_t NssaType = OSPFV3_LSA_NSSA_EXTERNAL;
};

// One LSDB record.
struct LsaRecord final
{
    std::chrono::steady_clock::time_point installTime;
    std::chrono::steady_clock::time_point lastRefreshTime;

    std::atomic<uint32_t> refCnt;

    LsaHeader header{};
    LsaBody body{};

    uint32_t incomingInterface;
    uint32_t incomingNeighbor;

    LsaRecordFlags flags{LsaRecordFlags::NONE};
    uint8_t pad[7]{};
};

// Lsa Ref
struct LsaRecordRef final
{
    LsaKey key;
    LsaRecord* record;

    LsaRecordRef() = default;

    LsaRecordRef(const LsaKey& k, LsaRecord& r) : key(k), record(&r)
    {
        record->refCnt.fetch_add(1, std::memory_order_relaxed);
    }

    LsaRecordRef(LsaRecordRef&& other) noexcept
        : key(std::move(other.key)), record(other.record)
    {
        other.record = nullptr;
    }

    LsaRecordRef(const LsaRecordRef& other)
        : key(other.key), record(other.record)
    {
        if (record) record->refCnt.fetch_add(1, std::memory_order_relaxed);
    }

    LsaRecordRef& operator=(LsaRecordRef&& other) noexcept
    {
        if (this != &other)
        {
            if (record) record->refCnt.fetch_sub(1, std::memory_order_relaxed);

            key = std::move(other.key);
            record = other.record;
            other.record = nullptr;
        }
        return *this;
    }

    LsaRecordRef& operator=(const LsaRecordRef& other)
    {
        if (this != &other)
        {
            if (record)
                record->refCnt.fetch_sub(1, std::memory_order_relaxed);

            key = other.key;
            record = other.record;

            if (record)
                record->refCnt.fetch_add(1, std::memory_order_relaxed);
        }
        return *this;
    }

    ~LsaRecordRef()
    {
        if (record) record->refCnt.fetch_sub(1, std::memory_order_acq_rel);
    }
};

using U_LSDB = UMap<LsaKey, LsaRecord*>;
using A_LSDB = UMap<LsaAdvKey, UMap<uint32_t, LsaRecord*>>;
using T_LSDB = UMap<uint32_t, UMap<LsaKey, LsaRecord*>>;
using O_LSDB = OMap<LsaKey, LsaRecord>;

} // namespace OSPF

#endif // OSPF_LSDB_H
