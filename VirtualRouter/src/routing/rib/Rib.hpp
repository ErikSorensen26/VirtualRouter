// Rib.hpp

#ifndef RIB_HPP
#define RIB_HPP

#include <unordered_map>
#include <atomic>
#include <cstdint>
#include "RibBucket.hpp"
#include "Fib.hpp"
#include <RCU.hpp>

template <typename AddrType>
class Rib
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

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
            return std::hash<AddrType>()(k.prefix) ^ (std::hash<uint8_t>()(k.length) << 1);
        }
    };

    std::atomic<std::unordered_map<PrefixKey, std::atomic<RibBucket<AddrType>*>, PrefixHash>*> table;
    Fib<AddrType> fib;

public:
    Rib() = default;
    ~Rib() { clear(); }

    void addRoute(const RibEntry<AddrType>& e)
    {
        PrefixKey key{ mask(e.prefix, e.length), e.length };
        auto& bucketPtr = table[key];

        RibBucket<AddrType>* oldBucket = bucketPtr.load(std::memory_order_acquire);
        if (!oldBucket)
        {
            FibEntry<AddrType>* fe = fib.getEntry(key.prefix, key.length);
            RibBucket<AddrType>* newBucket = new RibBucket<AddrType>(fe);
            newBucket->routes.push_back(e);
            newBucket->selectBest();
            bucketPtr.store(newBucket, std::memory_order_release);
            return;
        }

        RibBucket<AddrType>* newBucket = oldBucket->addRoute(e);
        bucketPtr.store(newBucket, std::memory_order_release);
        RCU::retire([oldBucket]{ delete oldBucket; });
    }

    void removeRoute(AddrType prefix, uint8_t length, AddrType nextHop)
    {
        PrefixKey key{ mask(prefix, length), length };
        auto it = table.find(key);
        if (it == table.end()) return;

        RibBucket<AddrType>* oldBucket = it.second.load(std::memory_order_acquire);
        if (!oldBucket) return;

        RibBucket<AddrType>* newBucket = oldBucket->removeRoute(nextHop);

        if (newBucket->empty())
        {
            fib.clearEntry(prefix, length);
            it->second.store(std::memory_order_release);
            RCU::retire([oldBucket, newBucket]{ delete oldBucket; delete newBucket; });
        }
        else
        {
            it->second.store(newBucket, std::memory_order_release);
            RCU::retire([oldBucket]{delete oldBucket; });
        }
    }

    FibEntry<AddrType> lookupFib(AddrType addr, RCU::ThreadEpoch* te) noexcept
    {
        return fib.lookup(addr, te);
    }

    void clear() noexcept
    {
        for (auto& [key, bucketPtr] : table)
        {
            RibBucket<AddrType>* b = bucketPtr.exchange(nullptr, std::memory_order_acq_rel);
            if (b) delete b;
        }
        table.clear();
        fib.clear();
    }

    size_t size() const noexcept { return table.size(); }

private:
    static constexpr uint8_t bitWidth() noexcept { return sizeof(AddrType) * 8; }

    static constexpr AddrType mask(AddrType prefix, uint8_t length) noexcept
    {
        if (length == 0) return 0;
        if (length >= bitWidth()) return prefix;
        AddrType m = (~AddrType(0)) << (bitWidth() - length);
        return prefix & m;
    }
};

#endif // RIB_HPP
