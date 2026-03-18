// RoutingTable.hpp

#ifndef ROUTING_TABLE_HPP
#define ROUTING_TABLE_HPP

#include <cstdint>
#include <AddressFamily.hpp>
#include <type_traits>

#include "rib/Rib.hpp"

class RoutingTable
{
    Rib<uint32_t> rib4;
    Rib<__uint128_t> rib6;

public:
    RoutingTable(ControlScheduler& cs)
        : rib4(cs.create()), rib6(cs.create())
    {}

    ~RoutingTable()
    {
        clearAll();
    }

    template <typename AddrType>
    bool addRoute(const RibEntry<AddrType>* entry)
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
    void addRoutes(const std::vector<RibEntry<AddrType>*>& entries)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            rib4.addRoutes(entries);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            rib6.addRoutes(entries);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    bool removeRoute(AddrType prefix, uint8_t length, RouteSource src, uint32_t pid)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.removeRoute(prefix, length, src, pid);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.removeRoute(prefix, length, src, pid);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
        return false;
    }

    template <typename AddrType>
    void removeRoutes(std::vector<std::pair<AddrType, uint8_t>> withdraws, RouteSource src, uint32_t pid)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.removeRoutes(withdraws, src, pid);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.removeRoutes(withdraws, src, pid);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    RibEntry<AddrType>* lookup(AddrType addr)
    {
        RCU::Guard g;
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.lookup(addr);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.lookup(addr);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    RibEntry<AddrType>* lookup(const uint8_t* addr)
    {
        RCU::Guard g;
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.lookup(addr);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.lookup(addr);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    RibEntry<AddrType>* lookup(AddrType a, uint32_t procId)
    {
        RCU::Guard g;
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.lookup(a, procId);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.lookup(a, procId);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    RibEntry<AddrType>* lookup(AddrType a, uint32_t procId, RouteSource source)
    {
        RCU::Guard g;
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.lookup(a, procId, source);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.lookup(a, procId, source);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    template <typename AddrType>
    typename Rib<AddrType>::WatchId watchRoute(AddrType prefix, uint8_t length, void* ctx,
                                               typename Rib<AddrType>::Callback fn)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.watchRoute(prefix, length, ctx, fn);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.watchRoute(prefix, length, ctx, fn);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
        return 0;
    }

    template <typename AddrType>
    typename Rib<AddrType>::WatchId watchAddress(AddrType addr, void* ctx,
                                                  typename Rib<AddrType>::Callback fn)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.watchAddress(addr, ctx, fn);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.watchAddress(addr, ctx, fn);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
        return 0;
    }

    template <typename AddrType>
    typename Rib<AddrType>::WatchId watchProtocol(RouteSource src, uint64_t pid, void* ctx,
                                  typename Rib<AddrType>::Callback fn)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.watchProtocol(src, pid, ctx, fn);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.watchProtocol(src, pid, ctx, fn);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
        return 0;
    }

    void unwatchAddress(uint32_t id, bool isV6)
    {
        if (isV6) rib6.unwatchRoute(id);
        else      rib4.unwatchRoute(id);
    }

    void clearAll() noexcept
    {
        rib4.clear();
        rib6.clear();
        RCU::synchronize();
        RCU::tryReclaim();
    }

private:
    template <typename T> static constexpr bool always_false = false;
};

#endif // ROUTING_TABLE_HPP
