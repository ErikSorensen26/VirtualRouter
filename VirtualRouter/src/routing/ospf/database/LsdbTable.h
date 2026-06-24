/**
 * @file LsdbTable.h
 * @brief OSPF LSDB table: aggregated LSA storage and multi-index access per area.
 */

#ifndef OSPF_LSDB_TABLE_H
#define OSPF_LSDB_TABLE_H

#include <utility>
#include <type_traits>

#include "LSDB.hpp"

namespace routing::ospf
{

/**
 * @brief Unified LSDB container that maintains multiple synchronized indexes over `LsaRecord` objects.
 * @ingroup OSPF_DATABASE
 *
 * `LsdbTable` is the single authoritative store for all LSAs within one OSPF
 * area or AS-scope.  It owns the `LsaRecord` objects (via `O_LSDB`, the
 * ordered primary map) and maintains three additional pointer-based indexes:
 * - `U_LSDB` — unordered index for O(1) key lookups.
 * - `A_LSDB` — per-advertiser index for iterating all LSAs from one router.
 * - `T_LSDB` — per-type index for iterating all LSAs of a given type.
 *
 * All four structures are kept in sync on every insert, update, and erase.
 * The three pointer indexes never own the records; they point into `O_LSDB`.
 *
 * ## Architectural Role
 * `LsdbTable` sits between the flood manager (which calls `upsertMeta` /
 * `upsertBody` when a new or updated LSA arrives) and the SPF engine (which
 * iterates via `forEachInType` and `forEachInAdv`).  It does not perform
 * flooding decisions or SPF computation; those responsibilities belong to
 * `FloodManager` and `SpfEngine` respectively.
 *
 * ## Lifecycle & Ownership
 * Created and owned by the OSPF `Area`.  Construction requires a
 * `std::pmr::memory_resource*` for the PMR-backed internal containers; when
 * PMR is disabled this parameter is ignored.  Move and copy are deleted
 * because the pointer indexes would be invalidated by a container relocation.
 *
 * ## Concurrency Model
 * All public methods must be called from the area's single-threaded
 * `ProcessQueue`.  There is no internal locking; the OSPF scheduler enforces
 * the single-writer invariant.
 *
 * @warning Move construction and assignment are deleted.  Do not attempt to
 * move an `LsdbTable` after construction; pointer indexes would be dangling.
 *
 * @see LsaRecord
 * @see FloodManager
 */
class LsdbTable final
{
public:
#if OSPF_LSDB_USE_PMR
    using PoolResource =
    std::pmr::unsynchronized_pool_resource; ///< Pool resource type used for LSDB allocation when PMR is enabled.
#endif

    /**
     * @brief Constructs an empty LSDB table, optionally using a custom memory resource.
     *
     * When `OSPF_LSDB_USE_PMR` is enabled, all internal containers allocate
     * from a pool resource that sub-allocates from `upstream`.  This allows the
     * entire LSDB to be released at once by discarding the pool rather than
     * individually freeing each record allocation.
     *
     * @param upstream PMR upstream resource; defaults to the global default resource.
     */
    explicit LsdbTable(std::pmr::memory_resource* upstream = std::pmr::get_default_resource());

    // Movable
    LsdbTable(LsdbTable&&) noexcept = delete;
    LsdbTable& operator=(LsdbTable&&) noexcept = delete;

    /**
     * @brief Returns the memory resource used for LSDB storage.
     *
     * When PMR is disabled this always returns `std::pmr::get_default_resource()`.
     */
    std::pmr::memory_resource* resource() noexcept;

    // CAPACITY

    /**
     * @brief Pre-allocates bucket capacity in the primary unordered index.
     *
     * Useful at startup when the expected LSA count is known, to avoid rehash
     * overhead during initial database synchronization.
     *
     * @param n Number of entries to reserve capacity for.
     */
    void reserve(size_t n);
    size_t size() const noexcept;
    bool empty() const noexcept;

    // LOOKUP

    bool contains(const LsaKey& key) const;
    LsaRecord* find(const LsaKey& key);
    const LsaRecord* find(const LsaKey& key) const;

    // MUTATION

    /**
     * @brief Clears all records and releases pooled memory back to the upstream resource.
     *
     * More aggressive than `clear()`: after this call the pool itself is
     * released, freeing all allocations in bulk.  Use during area teardown or
     * full LSDB flush to avoid O(n) individual deallocations.
     */
    void releaseMemory();

    /**
     * @brief Removes the record for the given key from all indexes.
     *
     * @param key Full LSA key to remove.
     * @return True if the key was present and removed, false if not found.
     *
     * @warning Do not erase a record with a non-zero `refCnt`; outstanding
     * `LsaRecordRef` holders would be left with a dangling pointer.
     */
    bool erase(const LsaKey& key);
    void clear();

    /**
     * @brief Removes all records satisfying a predicate and returns the count erased.
     *
     * Iterates the primary store and erases every entry for which
     * `pred(key, record)` returns true.
     *
     * @tparam Pred Callable with signature `bool(const LsaKey&, LsaRecord&)`.
     * @param pred  Predicate invoked for each record.
     * @return Number of records removed.
     */
    template <typename Pred>
    size_t purgeIf(Pred&& pred);

    /**
     * @brief Inserts or updates the header metadata for an LSA, keeping all indexes in sync.
     *
     * If the key is new, a fresh `LsaRecord` is inserted into the primary store
     * and all pointer indexes are updated.  If the key already exists, the
     * stored `LsaHeader` and `flags` are updated in place.
     *
     * @param lsa   Context carrying the key, header, and provenance of the incoming LSA.
     * @param flags Record-level flags to set (e.g. SELF_ORIGINATED, CHECKSUM_VALID).
     * @return Reference to the (possibly newly created) `LsaRecord`.
     */
    LsaRecord& upsertMeta(const IncomingLsaContext& lsa, LsaRecordFlags flags);

    /**
     * @brief Inserts or updates a typed LSA body, then calls `upsertMeta` for the header.
     *
     * Calls `upsertMeta` to ensure the record exists and metadata is current,
     * then emplaces the decoded body of type `Body` into `LsaRecord::body`.
     * If `Body` requires a memory resource (i.e. it has a `Body(mr*)` constructor
     * and PMR is enabled), the LSDB's pool resource is passed automatically.
     *
     * @tparam Body  Decoded LSA body type; must be one of the alternatives in `LsaBody`.
     * @param lsa    Context carrying the key, header, and provenance.
     * @param flags  Record-level flags to set.
     * @return Reference to the newly emplaced `Body` within the record.
     */
    template <typename Body>
    Body& upsertBody(const IncomingLsaContext& lsa, LsaRecordFlags flags);

    // CONVENIENCE

    /**
     * @brief Updates `lastRefreshTime` for a self-originated LSA after a periodic refresh.
     *
     * @param key Key of the LSA to touch.
     * @return True if the key was found and updated.
     */
    bool touchRefresh(const LsaKey& key);

    /**
     * @brief Sets additional flags on an existing record without replacing the header or body.
     *
     * @param key   Key of the record to modify.
     * @param flags Flags to OR into the existing record flags.
     * @return True if the key was found and updated.
     */
    bool setFlags(const LsaKey& key, LsaRecordFlags flags);

    // ITERATION

    /**
     * @brief Invokes `fn` for every valid record in the LSDB.
     *
     * @tparam Fn Callable with signature `void(const LsaKey&, LsaRecord&)`.
     */
    template <typename Fn>
    void forEach(Fn&& fn) const;

    /**
     * @brief Invokes `fn` for every record with the given advertiser key (type + advertising router).
     *
     * Uses the `A_LSDB` secondary index for O(degree) iteration without
     * scanning the full database.
     *
     * @tparam Fn  Callable with signature `void(const LsaKey&, LsaRecord&)`.
     * @param key  Partial key identifying the advertising router and LSA type.
     */
    template <typename Fn>
    void forEachInAdv(const LsaAdvKey& key, Fn&& fn) const;

    /**
     * @brief Invokes `fn` for every record of the specified LSA type.
     *
     * Uses the `T_LSDB` type index for O(count of type) iteration.
     *
     * @tparam Fn   Callable with signature `void(const LsaKey&, LsaRecord&)`.
     * @param type  LSA type constant (e.g. `OSPFV2_LSA_ROUTER`).
     */
    template <typename Fn>
    void forEachInType(uint32_t type, Fn&& fn) const;

    /**
     * @brief Returns the number of installed records of a given LSA type.
     *
     * @param type LSA type constant.
     */
    size_t getTypeSize(uint32_t type);

    // AGING

    /**
     * @brief Advances the age of every record by `deltaAge` seconds, clamping at `maxAge`.
     *
     * @param deltaAge    Seconds to add to each record's stored age.
     * @param maxAge      Age ceiling; records saturate here rather than wrapping.
     * @param eraseExpired If true, records that reach `maxAge` are removed immediately.
     * @return Number of records that reached `maxAge` during this pass.
     */
    size_t ageAll(uint16_t deltaAge, uint16_t maxAge, bool eraseExpired);

    /**
     * @brief Removes all records whose stored age equals `maxAge`.
     *
     * Called separately from `ageAll` when deferred expiry is preferred.
     *
     * @param maxAge Age threshold; records at or above this value are purged.
     * @return Number of records removed.
     */
    size_t purgeExpired(uint16_t maxAge);

    /**
     * @brief Scans all DC-capable LSAs and verifies DC-bit consistency across the database.
     *
     * Used for demand-circuit integrity checks (RFC 1793).  Returns false if
     * any inconsistency is detected.
     *
     * @return True if all DC-bit relationships are consistent.
     */
    bool runDCIntegrityScan();

    O_LSDB& getIterableLSDB() { return dbStorage; }
    const O_LSDB& getIterableLSDB() const { return dbStorage; }
    T_LSDB& getIterableTypeLSDB() { return typeDb; }
    const T_LSDB& getIterableTypeLSDB() const { return typeDb; }

private:

#if OSPF_LSDB_USE_PMR
    PoolResource pool;  ///< Pool sub-allocator; sub-allocates from the upstream resource passed at construction.
#endif
    U_LSDB db;          ///< Unordered primary index for O(1) key lookups; points into dbStorage.
    T_LSDB typeDb;      ///< Type-keyed secondary index; points into dbStorage.
    A_LSDB advDb;       ///< Advertiser-keyed secondary index; points into dbStorage.
    O_LSDB dbStorage;   ///< Ordered primary store; owns all LsaRecord objects.

private:
    using Iterator = decltype(db.begin());
    using ConstIterator = decltype(db.cbegin());

    Iterator findIt(const LsaKey& key);
    ConstIterator findIt(const LsaKey& key) const;

    template <typename Body>
    static constexpr bool BodyNeedsMr =
#if OSPF_LSDB_USE_PMR
        std::is_constructible_v<Body, std::pmr::memory_resource*>;
#else
        false;
#endif

    /**
     * @brief Emplaces a `Body` into the record's variant, passing the pool resource when required.
     *
     * If `Body` has a constructor accepting `std::pmr::memory_resource*` and
     * PMR is enabled, the LSDB pool is forwarded so the body's internal
     * containers also allocate from the same pool.
     *
     * @tparam Body LSA body type to emplace.
     * @param rec   Record into which the body is emplaced.
     * @return Reference to the newly constructed body.
     */
    template <typename Body>
    Body& emplaceBody(LsaRecord& rec);

    /**
     * @brief Adds two ages with saturation at `maxAge`, avoiding uint16_t overflow.
     *
     * @param a      Current age.
     * @param b      Delta to add.
     * @param maxAge Ceiling value; result is clamped here.
     * @return `min(a + b, maxAge)`.
     */
    static inline uint16_t saturatingAddAge(uint16_t a, uint16_t b, uint16_t maxAge) noexcept
    {
        const uint32_t sum = static_cast<uint32_t>(a) + static_cast<uint32_t>(b);
        return static_cast<uint16_t>(sum >= maxAge ? maxAge : sum);
    }
};

template <typename Body>
inline Body& LsdbTable::upsertBody(const IncomingLsaContext& ctx, LsaRecordFlags flags)
{
    LsaRecord& rec = upsertMeta(ctx, flags);
    return emplaceBody<Body>(rec);
}

template <typename Body>
inline Body& LsdbTable::emplaceBody(LsaRecord& rec)
{
    if constexpr (BodyNeedsMr<Body>)
    {
        return rec.body.template emplace<Body>(resource());
    }
    else
    {
        return rec.body.template emplace<Body>();
    }
}

template <typename Fn>
inline void LsdbTable::forEach(Fn&& fn) const
{
    for (auto& kv : db)
        if (kv.second)
            fn(kv.first, *kv.second);
}

template <typename Fn>
inline void LsdbTable::forEachInAdv(const LsaAdvKey& advRtr, Fn&& fn) const
{
    auto it = advDb.find(advRtr);
    if (it == advDb.end()) return;
    for (auto& kv : it->second)
        if (kv.second)
            fn(kv.first, *kv.second);
}

template <typename Fn>
inline void LsdbTable::forEachInType(uint32_t type, Fn&& fn) const
{
    auto it = typeDb.find(type);
    for (const auto& kv : it->second)
        if (kv.second)
            fn(kv.first, *kv.second);
}

template <typename Pred>
inline size_t LsdbTable::purgeIf(Pred&& pred)
{
    size_t removed = 0;
    for (auto it = db.begin(); it != db.end();)
    {
        if (pred(it->first, *it->second))
        {
            it = db.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return removed;
}
} // namespace routing::ospf

#endif // OSPF_LSDB_TABLE_H
