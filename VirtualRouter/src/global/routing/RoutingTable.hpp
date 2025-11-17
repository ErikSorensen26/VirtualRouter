// RoutingTable.hpp

#ifndef ROUTING_TABLE_HPP
#define ROUTING_TABLE_HPP

#include <cstdint>
#include <Rib.hpp>
#include <AddressFamily.hpp>
#include <type_traits>

class RoutingTable
{
    Rib<uint32_t> rib4;
    Rib<__uint128_t> rib6;

    thread_local static inline RCU::ThreadEpoch* threadEpoch = nullptr;

public:
    RoutingTable()
    {
        if (!threadEpoch)
            threadEpoch = RCU::registerThread();
    }

    ~RoutingTable()
    {
        if (threadEpoch)
            RCU::unregisterThread(threadEpoch);
    }

    template <typename AddrType>
    bool addRoute(const RibEntry<AddrType>& entry)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.addRoute(entry);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.addRoute(entry);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
        return false;
    }

    template <typename AddrType>
    void addRoutes(const std::vector<RibEntry<AddrType>>& entries)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            for (const auto& entry : entries)
                rib4.addRoute(entry);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            for (const auto& entry : entries)
                rib6.addRoute(entry);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    void removeEntry(AddrType prefix, uint8_t length, RouteSource src, uint32_t pid)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            rib4.removeRoute(prefix, length, src, pid);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            rib6.removeRoute(prefix, length, src, pid);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    RibEntry<AddrType>* lookup(AddrType addr)
    {
        RCU::Guard g(threadEpoch);
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.lookupFib(addr, threadEpoch);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.lookupFib(addr, threadEpoch);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    void clearAll() noexcept
    {
        rib4.clear();
        rib6.clear();
        RCU::synchronize();
        RCU::tryReclaim();
    }

    template <typename AddrType>
    size_t size() const noexcept
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.size();
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.size();
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

private:
    template <typename T> static constexpr bool always_false = false;
};

#endif // ROUTING_TABLE_HPP
