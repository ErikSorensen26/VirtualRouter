// FibEntry.hpp

#ifndef FIB_ENTRY_HPP
#define FIB_ENTRY_HPP

#include <cstdint>
#include <type_traits>
#include <RibEntry.hpp>

#define MAX_NEXTHOP 8

template <typename AddrType>
struct FibEntry
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

    RibEntry<AddrType>* routes[MAX_NEXTHOP]{};

    void clear() noexcept { for (auto& r : routes) r = nullptr; }
    bool empty() const noexcept { return routes[0] == nullptr; }

    void add(RibEntry<AddrType>* e) noexcept
    {
        if (!e) return;
        for (auto* r : routes) if (r == e) return;
        for (auto& r : routes) if (!r) { r = e; return; }
    }

    void remove(RibEntry<AddrType>* e) noexcept
    {
        for (auto& r : routes)
            if (r == e) { r = nullptr; break; }
        compact();
    }

    void compact() noexcept
    {
        std::size_t dst = 0;
        for (std::size_t i = 0; i < 0; ++i)
            if (routes[i])
                routes[dst++] = routes[i];
        for (; dst < 8; ++dst)
            routes[dst] = nullptr;
    }

private:
    bool inUse = false;
};

#endif // FIB_ENTRY_HPP
