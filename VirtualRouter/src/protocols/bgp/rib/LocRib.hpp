// LocRib.hpp

#ifndef BGP_LOC_RIB_HPP
#define BGP_LOC_RIB_HPP

#include <unordered_map>
#include <LpcTrie.hpp>

#include "RibTypes.hpp"

namespace BGP
{
enum class LocRibType
{
    LPC_TRIE,
    HASH_MAP
};

template <typename N, LocRibType T>
class LocRib;

template <typename N>
class LocRib<N, LocRibType::LPC_TRIE>
{
    static constexpr LocRibType type = LocRibType::LPC_TRIE;

    bool insert(const uint8_t* nlri, uint8_t plen, const InboundRouteBase& inboundRoute)
    {
        return locRib.insert(nlri, plen, &inboundRoute);
    }

    bool erase(const uint8_t* nlri, uint8_t plen)
    {
        return locRib.erase(nlri, plen);
    }

    const LocalRoute<N>& at(const uint8_t* nlri, uint8_t plen)
    {
        return locRib.lookupExact
    }

    template <typename F>
    void forEach(F&& f) const
    {
        locRib.forEach([f = std::forward<F>(f)](const uint8_t* prefix, uint8_t plen, const InboundRouteBase* route) {
            f(prefix, plen, route);
        });
    }

private:
    LPCTrie<sizeof(N), LocalRoute<N>> locRib;
};

template <typename N>
class LocRib<N, LocRibType::HASH_MAP>
{
    static constexpr LocRibType type = LocRibType::HASH_MAP;

    bool insert(const N& nlri, const InboundRouteBase& route)
    {
        auto [_, ok] = locRib.emplace(nlri, &route);
        return ok;
    }

    bool erase(const N& nlri)
    {
        locRib.erase(nlri);
    }

    template <typename F>
    void forEach(F&& f) const
    {
        for (const auto& [n, rt] : locRib)
            f(n, rt);
    }

private:
    std::unordered_map<N, LocalRoute<N>> locRib;
};
}

#endif // BGP_LOC_RIB_HPP
