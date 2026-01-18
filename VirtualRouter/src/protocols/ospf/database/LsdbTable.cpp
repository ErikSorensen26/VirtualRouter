// LsdbTable.cpp

#include "LsdbTable.h"

namespace OSPF
{
LsdbTable::LsdbTable(std::pmr::memory_resource* upstream, std::size_t initialReserve)
#if OSPF_LSDB_USE_PMR
    : pool(upstream), db(&pool), dbStorage(&pool)
#else
    : db()
#endif
{
    if (initialReserve)
        reserve(initialReserve);
}

void LsdbTable::reserve(size_t n)
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif
    db.reserve(n);
}

size_t LsdbTable::size() const noexcept
{
#if OSPF_LSDB_THREADSAFE
    std::shared_lock<std::shared_mutex> lk(mu);
#endif
    return db.size();
}

bool LsdbTable::empty() const noexcept
{
#if OSPF_LSDB_THREADSAFE
    std::shared_lock<std::shared_mutex> lk(mu);
#endif
    return db.empty();
}

bool LsdbTable::contains(const LsaKey& key) const
{
#if OSPF_LSDB_THREADSAFE
    std::shared_lock<std::shared_mutex> lk(mu);
#endif
    return db.find(key) != db.end();
}

LsdbTable::Iterator LsdbTable::findIt(const LsaKey& key)
{
    return db.find(key);
}

LsdbTable::ConstIterator LsdbTable::findIt(const LsaKey& key) const
{
    return db.find(key);
}

LsaRecord* LsdbTable::find(const LsaKey& key)
{
#if OSPF_LSDB_THREADSAFE
    std::shared_lock<std::shared_mutex> lk(mu);
#endif
    auto it = findIt(key);
    return (it == db.end()) ? nullptr : it->second;
}

const LsaRecord* LsdbTable::find(const LsaKey& key) const
{
#if OSPF_LSDB_THREADSAFE
    std::shared_lock<std::shared_mutex> lk(mu);
#endif
    auto it = findIt(key);
    return (it == db.end()) ? nullptr : it->second;
}

bool LsdbTable::erase(const LsaKey& key)
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif
    db.erase(key);
    auto& adv = advDb[key];
    adv.erase(key.linkStateId);
    auto& type = typeDb[key.lsaType];
    type.erase(key);
    if (adv.empty()) advDb.erase(key);
    if (type.empty()) typeDb.erase(key.lsaType);
    return dbStorage.erase(key) != 0;
}

void LsdbTable::clear()
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif
    db.clear();
    advDb.clear();
    typeDb.clear();
    dbStorage.clear();
}

void LsdbTable::releaseMemory()
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif
    db.clear();
    advDb.clear();
    typeDb.clear();
    dbStorage.clear();
#if OSPF_LSDB_USE_PMR
    pool.release();
#endif
}

LsaRecord& LsdbTable::upsertMeta(const IncomingLsaContext& lsa, LsaRecordFlags flags)
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif

    // Single lookup + in-place default construction if missing.
    auto [it, inserted] = dbStorage.try_emplace(lsa.key);
    if (inserted) 
    {
        db[lsa.key] = &it->second;
        advDb[lsa.key][lsa.key.linkStateId] = &it->second;
        typeDb[lsa.key.lsaType][lsa.key] = &it->second;
    }

    LsaRecord& rec = it->second;

    // Meta update is allocation free.
    rec.header = lsa.header;
    rec.flags = flags;
    rec.incomingInterface = lsa.incomingInterface;
    rec.incomingNeighbor = lsa.incomingNeighbor;

    auto now = std::chrono::steady_clock::now();

    if (inserted)
        rec.installTime = now;

    rec.lastRefreshTime = now;
    return rec;
}

bool LsdbTable::touchRefresh(const LsaKey& key)
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif
    auto it = db.find(key);
    if (it == db.end())
        return false;

    it->second->lastRefreshTime = std::chrono::steady_clock::now();
    return true;
}

bool LsdbTable::setFlags(const LsaKey& key, LsaRecordFlags flags)
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif
    auto it = db.find(key);
    if (it == db.end())
        return false;

    it->second->flags = flags;
    return true;
}

size_t LsdbTable::getTypeSize(uint32_t type)
{
    std::shared_lock<std::shared_mutex> lk(mu);
    auto it = typeDb.find(type);
    if (it == typeDb.end()) return 0;
    return it->second.size();
}

size_t LsdbTable::ageAll(uint16_t deltaAge, uint16_t maxAge, bool eraseExpired)
{
#if OSPF_LSDB_THREADSAFE
    std::unique_lock<std::shared_mutex> lk(mu);
#endif

    size_t expired = 0;

    if (!eraseExpired)
    {
        for (auto& kv : dbStorage)
        {
            LsaHeader& h = kv.second.header;
            h.age = saturatingAddAge(h.age, deltaAge, maxAge);
            if (h.age >= maxAge)
                ++expired;
        }
        return expired;
    }

    for (auto it = dbStorage.begin(); it != dbStorage.end();)
    {
        LsaHeader& h = it->second.header;
        h.age = saturatingAddAge(h.age, deltaAge, maxAge);

        if (h.age >= maxAge)
        {
            db.erase(it->first);
            auto& adv = advDb[it->first];
            adv.erase(it->first.linkStateId);
            auto& type = typeDb[it->first.lsaType];
            type.erase(it->first);
            if (adv.empty()) advDb.erase(it->first);
            if (type.empty()) typeDb.erase(it->first.lsaType);
            it = dbStorage.erase(it);
            ++expired;
        }
        else
        {
            ++it;
        }
    }

    return expired;
}

size_t LsdbTable::purgeExpired(uint16_t maxAge)
{
    return purgeIf([&](const LsaKey&, const LsaRecord& r){
        return r.header.age >= maxAge;
    });
}
}
