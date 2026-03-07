// LocRib.hpp

#ifndef BGP_LOC_RIB_HPP
#define BGP_LOC_RIB_HPP

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BGP
{
template <class KeyT, class BestT = KeyT>
class LocRib final
{
public:
    using KeyType = KeyT;
    using BestType = BestT;

    struct Entry
    {
        BestT best;
        uint64_t version = 0;
    };

    bool contains(const KeyT& k) const
    {
        return rib.find(k) != rib.end();
    }

    const Entry* find(const KeyT& k) const
    {
        auto it = rib.find(k);
        return (it == rib.end()) ? nullptr : &it->second;
    }

    Entry* find(const KeyT& k)
    {
        auto it = rib.find(k);
        return (it == rib.end()) ? nullptr : &it->second;
    }

    template <class BestEqual>
    bool installOrReplace(const KeyT& k, BestT best, BestEqual&& eq)
    {
        auto it = rib.find(k);
        if (it == rib.end())
        {
            Entry e;
            e.best = std::move(best);
            e.version = ++globalVersion;
            rib.emplace(k, std::move(e));
            return true;
        }

        if (eq(it->second.best, best))
            return false; // unchanged

        it->second.best = std::move(best);
        it->second.version = ++globalVersion;
        return true;
    }

    bool remove(const KeyT& k)
    {
        return rib.erase(k) != 0;
    }

    void forEach(auto&& fn) const
    {
        for (const auto& [k, e] : rib)
            fn(k, e);
    }

private:
    std::unordered_map<KeyT, Entry> rib;
    uint64_t globalVersion = 0;
};
}

#endif // BGP_LOC_RIB_HPP
