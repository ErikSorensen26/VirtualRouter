// Fib.hpp

#ifndef FIB_HPP
#define FIB_HPP

#include <atomic>
#include <cstdint>
#include <LpcTrie.hpp>

#include "routing/rib/RibEntry.hpp"

template<typename Addr>
class Fib
{
public:
    using FibEntry = std::atomic<RibEntry<Addr>*>;

    // byte-array API (network order)
    RibEntry<Addr>* lookup(const uint8_t* a) const
    {
        FibEntry* fe = tree.lookup(a);
        return fe ? fe->load(std::memory_order_relaxed) : nullptr;
    }

    bool insert(const uint8_t* pfx, uint8_t len, FibEntry* ribEntry)
    {
        return tree.insert(pfx, len, ribEntry);
    }

    bool erase(const uint8_t* p, uint8_t l)
    {
        return tree.erase(p, l);
    }

    // integer API — converts to network-order bytes internally
    RibEntry<Addr>* lookup(Addr a) const
    {
        uint8_t bytes[sizeof(Addr)];
        toBytes(a, bytes);
        return lookup(bytes);
    }

    bool insert(Addr pfx, uint8_t len, FibEntry* ribEntry)
    {
        uint8_t bytes[sizeof(Addr)];
        toBytes(pfx, bytes);
        return insert(bytes, len, ribEntry);
    }

    bool erase(Addr pfx, uint8_t l)
    {
        uint8_t bytes[sizeof(Addr)];
        toBytes(pfx, bytes);
        return erase(bytes, l);
    }

    static void toBytes(Addr a, uint8_t* out) noexcept
    {
        for (uint8_t i = 0; i < sizeof(Addr); ++i)
            out[i] = static_cast<uint8_t>(a >> ((sizeof(Addr) - 1 - i) * 8));
    }

    void clear() { tree.clear(); }

private:
    static constexpr uint8_t W = sizeof(Addr)*8;

    LPCTrie<sizeof(Addr), FibEntry, 8, true> tree;
};

#endif // FIB_HPP
