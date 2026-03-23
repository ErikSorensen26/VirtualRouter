// LsdbTable.hpp

#ifndef OSPF_LSDB_TABLE_H
#define OSPF_LSDB_TABLE_H

#include <utility>
#include <type_traits>

#include "LSDB.hpp"

namespace routing::ospf
{
class LsdbTable final
{
public:
#if OSPF_LSDB_USE_PMR
    using PoolResource = 
    std::pmr::unsynchronized_pool_resource;
#endif

    explicit LsdbTable(std::pmr::memory_resource* upstream = std::pmr::get_default_resource());

    // Movable
    LsdbTable(LsdbTable&&) noexcept = delete;
    LsdbTable& operator=(LsdbTable&&) noexcept = delete;

    // Memory resource used for LSDB storage (when PMR enabled; otherwide returns default_resource()).
    std::pmr::memory_resource* resource() noexcept;

    // Capacity management
    void reserve(size_t n);
    size_t size() const noexcept;
    bool empty() const noexcept;

    // Basic map operations
    bool contains(const LsaKey& key) const;
    LsaRecord* find(const LsaKey& key);
    const LsaRecord* find(const LsaKey& key) const;

    // Clear the table and released pooled memory back to the upstream resource.
    void releaseMemory();
    bool erase(const LsaKey& key);
    void clear();

    template <typename Pred>
    size_t purgeIf(Pred&& pred);

    LsaRecord& upsertMeta(const IncomingLsaContext& lsa, LsaRecordFlags flags);
    template <typename Body>
    Body& upsertBody(const IncomingLsaContext& lsa, LsaRecordFlags flags);

    // Convenience
    bool touchRefresh(const LsaKey& key);
    bool setFlags(const LsaKey& key, LsaRecordFlags flags);

    // Iteration (single pass; callback signature)
    template <typename Fn>
    void forEach(Fn&& fn) const;

    template <typename Fn>
    void forEachInAdv(const LsaAdvKey& key, Fn&& fn) const;

    template <typename Fn>
    void forEachInType(uint32_t type, Fn&& fn) const;

    size_t getTypeSize(uint32_t type);

    // Aging helpers
    size_t ageAll(uint16_t deltaAge, uint16_t maxAge, bool eraseExpired);
    size_t purgeExpired(uint16_t maxAge);

    // Other
    bool runDCIntegrityScan();

    O_LSDB& getIterableLSDB() { return dbStorage; }
    const O_LSDB& getIterableLSDB() const { return dbStorage; }
    T_LSDB& getIterableTypeLSDB() { return typeDb; }
    const T_LSDB& getIterableTypeLSDB() const { return typeDb; }

private:

#if OSPF_LSDB_USE_PMR
    PoolResource pool;
    U_LSDB db;
    T_LSDB typeDb;
    A_LSDB advDb;
    O_LSDB dbStorage;
#else
    LSDB db
#endif

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

    template <typename Body>
    Body& emplaceBody(LsaRecord& rec);

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

