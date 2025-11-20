// Rib.hpp

#ifndef RIB_HPP
#define RIB_HPP

#include <unordered_map>
#include <atomic>
#include <mutex>
#include "Fib.hpp"
#include "RibBucket.hpp"

template <typename AddrType>
class Rib
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");
    static constexpr uint8_t W = sizeof(AddrType)*8;

    struct PrefixKey
    {
        AddrType prefix;
        uint8_t length;

        bool operator==(const PrefixKey& o) const noexcept
        {
            return prefix == o.prefix && length == o.length;
        }
    };

    struct PrefixHash
    {
        size_t operator()(const PrefixKey& k) const noexcept
        {
            uint64_t h1 = std::hash<AddrType>{}(k.prefix);
            uint64_t h2 = k.length;

            uint64_t hash = h1;
            hash ^= h2 + 0x9e3779b97f4a7c15 + (hash << 6) + (hash >> 2);
            return static_cast<size_t>(hash);
        }
    };

    static AddrType mask(AddrType p, uint8_t l)
    {
        if (l == 0) return 0;
        if (l >= W) return p;
        return p & (~AddrType(0) << (W - l));
    }

    std::unordered_map<PrefixKey, RibBucket<AddrType>*, PrefixHash> table;
    mutable std::mutex ribMtx;
    Fib<AddrType> fib;

public:
    Rib() = default;

    Rib(const Rib&) = delete;
    Rib& operator=(const Rib&) = delete;
    Rib(Rib&&) = delete;
    Rib& operator=(Rib&&) = delete;

    ~Rib() { clear(); }

    bool addRoute(const RibEntry<AddrType>& e)
    {
        std::lock_guard<std::mutex> lock(ribMtx);
        PrefixKey key{ mask(e.prefix, e.length), e.length };

        RibBucket<AddrType>* b = nullptr;

        auto it = table.find(key);
        if (it == table.end())
        {
            b = new RibBucket<AddrType>();
            if (!fib.insert(e.prefix, e.length, b->fibEntry)) throw std::runtime_error("Fib insert failed");
            b->addRoute(e);
            table[key] = b;
        }
        else
        {
            b = it->second;
            b->addRoute(e);
        }

        b->selectBest();

        return true;
    }

    bool removeRoute(AddrType prefix, uint8_t length, RouteSource src, uint32_t pid = 0)
    {
        std::lock_guard<std::mutex> lock(ribMtx);
        PrefixKey key{ mask(prefix, length), length };

        auto it = table.find(key);
        if (it == table.end()) return false;

        RibBucket<AddrType>* b = it->second;
        b->removeRoute(src, pid);

        if (b->empty())
        {
            table.erase(it);
            fib.erase(prefix, length);
            std::atomic<RibEntry<AddrType>*>* oldFibEntry = b->fibEntry;
            RCU::retire([oldFibEntry]{ delete oldFibEntry; });
            delete b;
            return true;
        }

        b->selectBest();

        return true;
    }

    void clear() noexcept
    {
        fib.clear();

        std::lock_guard<std::mutex> lock(ribMtx);
        for (auto& kv : table)
        {
            delete kv.second->fibEntry;
            delete kv.second;
        }

        table.clear();
    }

    RibEntry<AddrType>* lookup(AddrType addr)
    {
        return fib.lookup(addr);
    }

    size_t size() const noexcept
    {
        std::lock_guard<std::mutex> lock(ribMtx);
        return table.size();
    }
};

#endif // RIB_HPP
