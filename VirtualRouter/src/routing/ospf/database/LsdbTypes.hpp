/**
 * @file LsdbTypes.hpp
 * @brief OSPF Link State Database: LSA storage and retrieval.
 */

/**
 * @defgroup OSPF_DATABASE OSPF Database
 * @ingroup OSPF
 * @brief Link State Database, LSA key, and LSDB table types.
 */

#ifndef OSPF_LSDB_TYPES_H
#define OSPF_LSDB_TYPES_H

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <map>
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
#include "ospf/ospfv2/database/OpaqueLsaV2.hpp"

#include "ospf/ospfv3/database/RouterLsaV3.hpp"
#include "ospf/ospfv3/database/NetworkLsaV3.hpp"
#include "ospf/ospfv3/database/InterAreaPrefixLsa.hpp"
#include "ospf/ospfv3/database/InterAreaRouterLsa.hpp"
#include "ospf/ospfv3/database/ExternalLsaV3.hpp"
#include "ospf/ospfv3/database/LinkLsa.hpp"
#include "ospf/ospfv3/database/IntraAreaPrefixLsa.hpp"
#include "ospf/ospfv3/database/InterAreaPrefixLsaV4.hpp"
#include "ospf/ospfv3/database/IntraAreaPrefixLsaV4.hpp"

#include "ospf/flooding/FloodTypes.hpp"

namespace routing::ospf
{
template <typename T> using Vec = std::vector<T>;
using ByteVec = std::vector<std::byte>;
template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using UMap = std::unordered_map<K, V, H, E>;
template <typename K, typename H = std::hash<K>, typename E = std::equal_to<K>>
using USet = std::unordered_set<K, H, E>;
template <typename K, typename V, typename C = std::less<K>>
using OMap = std::map<K, V, C>;

/**
 * @brief Wire-derived header metadata stored for every installed LSA.
 * @ingroup OSPF_DATABASE
 *
 * Mirrors the common header fields present in every OSPFv2 and OSPFv3 LSA.
 * The `age` field is updated in-place during aging without touching the body,
 * which is why it is stored separately from the typed body variant.
 */
struct LsaHeader final
{
    uint32_t sequence{0}; ///< LSA sequence number; monotonically increasing per originator.
    uint16_t checksum{0}; ///< Fletcher checksum over the LSA excluding the age field.
    uint16_t length{0};   ///< Total wire length of the LSA including the header.
    uint16_t age{0};      ///< Current LSA age in seconds; saturates at MaxAge.
    uint8_t options{0};   ///< Options field (OSPFv2) or zero-padded (OSPFv3 uses per-LSA options).

    bool operator==(LsaHeader& rhs) const
    {
        return sequence == rhs.sequence &&
               checksum == rhs.checksum &&
               length == rhs.length &&
               age == rhs.age &&
               options == rhs.options;
    }
};
static_assert(sizeof(LsaHeader) == 12, "Unexpected LsaHeader size");

/**
 * @brief Compact per-record status flags packed into a single byte.
 * @ingroup OSPF_DATABASE
 */
enum class LsaRecordFlags : uint8_t
{
    NONE            = 0,       ///< No flags set.
    SELF_ORIGINATED = 1u << 0, ///< This LSA was originated by the local router.
    CHECKSUM_VALID  = 1u << 1, ///< Checksum was validated when the LSA was installed.
};

/**
 * @brief All information about one arriving LSA needed to evaluate and install it.
 * @ingroup OSPF_DATABASE
 *
 * Passed by the receive path into `LsdbTable::upsertMeta` / `upsertBody` so
 * that the LSDB can record provenance (incoming interface, neighbor) alongside
 * the LSA content.  The struct holds references into the receive buffer, so it
 * must not outlive the call stack that owns the buffer.
 *
 * @warning `key` and `header` are non-owning references; this struct must not
 * be stored beyond the scope of the LSDB install call.
 */
struct IncomingLsaContext final
{
    const LsaKey& key;
    LsaHeader& header;

    bool checksumValid{false};     ///< True if the Fletcher checksum was verified before install.
    bool selfOriginatedKey{false}; ///< True if the key matches a locally originated LSA.
    FloodInfo info{};

    uint32_t incomingInterface{0}; ///< Interface index the LSA arrived on.
    uint32_t incomingNeighbor{0};  ///< Neighbor router ID that sent the LSA.
};

constexpr inline LsaRecordFlags operator|(LsaRecordFlags a, LsaRecordFlags b) noexcept
{
    return static_cast<LsaRecordFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr inline LsaRecordFlags operator&(LsaRecordFlags a, LsaRecordFlags b) noexcept
{
    return static_cast<LsaRecordFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr inline LsaRecordFlags& operator|=(LsaRecordFlags& a, LsaRecordFlags b) noexcept
{
    a = (a | b);
    return a;
}
constexpr inline bool hasFlag(LsaRecordFlags v, LsaRecordFlags f) noexcept
{
    return (v & f) != LsaRecordFlags::NONE;
}

/**
 * @brief One node in a compact arena-style TLV tree stored inside an LSA record.
 * @ingroup OSPF_DATABASE
 *
 * OSPFv3 LSAs that carry nested TLV structures (e.g. Router-Information LSAs)
 * are decoded into an `LsaTlvForest`.  Each node records its value location
 * within the forest's flat byte blob and links to its first child and next
 * sibling by index, forming a forest of trees without pointer indirection.
 *
 * `0xFFFFFFFF` (kInvalid) is the sentinel for absent child/sibling links.
 */
struct LsaTlvNode final
{
    uint32_t valueOffset{0};            ///< Byte offset into LsaTlvForest::blob where this node's value begins.
    uint32_t valueLength{0};            ///< Length in bytes of this node's value.
    uint32_t firstChild{0xFFFFFFFFu};   ///< Index into LsaTlvForest::nodes of first child, or kInvalid.
    uint32_t nextSibling{0xFFFFFFFFu};  ///< Index into LsaTlvForest::nodes of next sibling, or kInvalid.
    uint16_t type{0};
    uint16_t reserved{0};
};
static_assert(sizeof(LsaTlvNode) == 20, "Unexpected LsaTlvNode size");

/**
 * @brief Arena-based forest of TLV nodes decoded from a single LSA's payload.
 * @ingroup OSPF_DATABASE
 *
 * All TLV values are stored contiguously in `blob`; `nodes` holds the
 * structural metadata (offsets, lengths, tree links).  Node index 0 is a
 * reserved root sentinel; real nodes start at index 1.
 */
struct LsaTlvForest final
{
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu; ///< Sentinel value meaning "no child / no sibling".

    ByteVec blob;       ///< Flat byte buffer holding all TLV values for this LSA.
    Vec<LsaTlvNode> nodes; ///< TLV node descriptors; index 0 is the root sentinel.

    explicit LsaTlvForest()
    { nodes.push_back(LsaTlvNode{}); }
};

/**
 * @brief Type-erased container holding the decoded body of any supported LSA type.
 * @ingroup OSPF_DATABASE
 *
 * `std::monostate` is the default (unparsed / body not yet decoded).
 * The active alternative is determined by the LSA type field in the
 * corresponding `LsaRecord::header`.  Callers use `std::get<T>` or
 * `std::get_if<T>` together with a Policy type to select the correct
 * alternative without switching on the raw type constant.
 */
using LsaBody = std::variant<
    std::monostate,
    RouterLsaV2,
    NetworkLsaV2,
    SummaryNetworkLsa,
    SummaryRouterLsa,
    ExternalLsaV2,
    OpaqueLsaV2,
    RouterLsaV3,
    NetworkLsaV3,
    InterAreaPrefixLsa,
    InterAreaRouterLsa,
    ExternalLsaV3,
    LinkLsa,
    IntraAreaPrefixLsa,
    InterAreaPrefixLsaV4,
    IntraAreaPrefixLsaV4
>;

/**
 * @brief OSPFv2 LSA type mappings: associates semantic LSA roles with OSPFv2 type constants.
 * @ingroup OSPF_DATABASE
 *
 * Passed as a template policy to generic code (originators, SPF, summary
 * processing) that must operate on OSPFv2 LSA types.  Decouples the
 * algorithms from the concrete type-constant values defined by the wire format.
 */
struct PolicyV2
{
    using RouterLsa      = RouterLsaV2;
    using NetworkLsa     = NetworkLsaV2;
    using InterNetworkLsa = SummaryNetworkLsa;
    using InterRouterLsa = SummaryRouterLsa;
    using ExternalLsa    = ExternalLsaV2;

    static constexpr uint16_t RouterLsaType    = OSPFV2_LSA_ROUTER;
    static constexpr uint16_t NetworkLsaType   = OSPFV2_LSA_NETWORK;
    static constexpr uint16_t InterNetworkType = OSPFV2_LSA_SUM_NET;
    static constexpr uint16_t InterRouterType  = OSPFV2_LSA_SUM_ASBR;
    static constexpr uint16_t ExternalType     = OSPFV2_LSA_EXTERNAL;
    static constexpr uint16_t NssaType         = OSPFV2_LSA_NSSA;
};

/**
 * @brief OSPFv3 LSA type mappings: associates semantic LSA roles with OSPFv3 type constants.
 * @ingroup OSPF_DATABASE
 *
 * Identical in structure to `PolicyV2` but references OSPFv3 body types and
 * wire-format type constants.  Allows all generic origination, SPF, and
 * summary code to be instantiated for either OSPF version by swapping policies.
 *
 * @see PolicyV2
 */
struct PolicyV3
{
    using RouterLsa      = RouterLsaV3;
    using NetworkLsa     = NetworkLsaV3;
    using InterNetworkLsa = InterAreaPrefixLsa;
    using InterRouterLsa = InterAreaRouterLsa;
    using ExternalLsa    = ExternalLsaV3;

    static constexpr uint16_t RouterLsaType    = OSPFV3_LSA_ROUTER;
    static constexpr uint16_t NetworkLsaType   = OSPFV3_LSA_NETWORK;
    static constexpr uint16_t InterNetworkType = OSPFV3_LSA_INTER_AREA_PREFIX;
    static constexpr uint16_t InterRouterType  = OSPFV3_LSA_INTER_AREA_ROUTER;
    static constexpr uint16_t ExternalType     = OSPFV3_LSA_AS_EXTERNAL;
    static constexpr uint16_t NssaType         = OSPFV3_LSA_NSSA_EXTERNAL;
};

/**
 * @brief One installed LSA record in the LSDB.
 * @ingroup OSPF_DATABASE
 *
 * Owns the decoded header metadata and typed body for a single LSA instance.
 * `refCnt` is an atomic reference count used by `LsaRecordRef` to track
 * outstanding references held by the flood queue, SPF engine, or any other
 * subsystem that retains a pointer to the record across an LSDB mutation.
 *
 * ## Lifecycle & Ownership
 * Records are owned by `O_LSDB` (the ordered primary store inside
 * `LsdbTable`).  Pointers into the map are valid as long as the record is not
 * erased; `LsaRecordRef` should be used whenever a pointer must survive a
 * potential erase by decrementing the ref count on destruction and signalling
 * to the LSDB that the record may be freed.
 *
 * @warning Do not erase a record whose `refCnt` is non-zero without first
 * draining all outstanding `LsaRecordRef` holders.
 */
struct LsaRecord final
{
    std::chrono::steady_clock::time_point installTime;    ///< When this instance was first installed.
    std::chrono::steady_clock::time_point lastRefreshTime; ///< When the age was last reset (self-originated refresh).

    mutable std::atomic<uint32_t> refCnt; ///< Outstanding LsaRecordRef holders; record must not be freed while non-zero.

    LsaHeader header{};
    LsaBody body{};

    uint32_t incomingInterface; ///< Interface on which this instance was received (0 for self-originated).
    uint32_t incomingNeighbor;  ///< Neighbor that sent this instance (0 for self-originated).

    LsaRecordFlags flags{LsaRecordFlags::NONE};
    uint8_t pad[7]{};
};

/**
 * @brief Reference-counted handle to an `LsaRecord` in the LSDB.
 * @ingroup OSPF_DATABASE
 *
 * Increments `LsaRecord::refCnt` on construction and decrements it on
 * destruction, ensuring that the LSDB knows when it is safe to reclaim a
 * record that has been scheduled for deletion.  Designed for short-lived
 * retention (flood queue entries, SPF graph snapshots) where the caller must
 * not hold a raw pointer across an LSDB write.
 *
 * Copy and move semantics mirror `std::shared_ptr`: copy increments the count,
 * move leaves the source with a null record pointer.
 *
 * @warning The underlying `LsaRecord` is not protected from concurrent writes
 * by the ref count alone.  The ref count only prevents *deallocation*; callers
 * must still observe the LSDB's single-writer discipline before reading mutable
 * fields like `header.age`.
 */
struct LsaRecordRef final
{
    LsaKey key;
    const LsaRecord* record;

    LsaRecordRef() : record(nullptr) {}

    LsaRecordRef(const LsaKey& k, const LsaRecord& r) : key(k), record(&r)
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

// LSDB INDEX TYPES

using U_LSDB = UMap<LsaKey, LsaRecord*>;   ///< Primary unordered index: full key → record pointer.
using A_LSDB = UMap<LsaAdvKey, UMap<uint32_t, LsaRecord*>>; ///< Advertiser index: (type, advRtr) → { lsid → record* }.
using T_LSDB = UMap<uint32_t, UMap<LsaKey, LsaRecord*>>;    ///< Type index: lsaType → { full key → record* }.
using O_LSDB = OMap<LsaKey, LsaRecord>;    ///< Ordered primary store; owns the LsaRecord objects.

} // namespace routing::ospf

#endif // OSPF_LSDB_TYPES_H
