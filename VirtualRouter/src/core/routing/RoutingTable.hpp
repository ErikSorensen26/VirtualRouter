/**
 * @file RoutingTable.hpp
 * @brief Global per-VRF RIB/FIB facade owning IPv4 and IPv6 Rib instances.
 */

/**
 * @defgroup CORE_ROUTING Core Routing
 * @ingroup CORE
 * @brief Per-VRF RIB/FIB facade, RIB buckets, FIB, and route watcher.
 */

#ifndef ROUTING_TABLE_HPP
#define ROUTING_TABLE_HPP

#include <cstdint>
#include <AddressFamily.hpp>
#include <type_traits>

#include "rib/Rib.hpp"

namespace core
{

/**
 * @brief The global per-VRF Routing Information Base and Forwarding
 *        Information Base facade.
 *
 * `RoutingTable` aggregates an IPv4 `Rib<uint32_t>` and an IPv6
 * `Rib<__uint128_t>` behind a single address-family-dispatching interface.
 * All routing protocols install and withdraw routes here; the data plane reads
 * the FIB via `lookup()`.
 *
 * Template dispatch is resolved at compile time with `if constexpr` so all
 * methods share a single, readable signature while specialising correctly for
 * each address type.
 *
 * ## Architectural Role
 * - Owned by the VRF/`VirtualRouter` and shared (by reference) with every
 *   routing protocol process.
 * - Routes flow in via `addRoute()` / `removeRoute()` (posted asynchronously
 *   to the RIB scheduler).
 * - The data plane calls `lookup()` synchronously under an `RCU::Guard`.
 * - BGP network-command and next-hop tracking use `watchRoute()` /
 *   `watchAddress()` / `unwatchAddress()`.
 *
 * ## Lifecycle & Ownership
 * Constructed with a `ControlScheduler` reference; each Rib creates its own
 * `ProcessQueue` from that scheduler.  The destructor calls `clearAll()` which
 * drains both RIBs, synchronises RCU, and reclaims deferred memory.
 *
 * ## Concurrency Model
 * - `addRoute`, `removeRoute`, `watchRoute`, `watchAddress`, `watchProtocol`,
 *   `unwatchAddress` — thread-safe; they post to the RIB scheduler.
 * - `lookup` — lock-free; caller must hold an `RCU::Guard`.
 * - `clearAll` — blocks until RCU quiesces; do not call from a reader thread.
 *
 * @see Rib
 * @see RouteWatcher
 */
class RoutingTable
{
    Rib<uint32_t>     rib4; ///< IPv4 Routing Information Base.
    Rib<__uint128_t>  rib6; ///< IPv6 Routing Information Base.

public:
    /**
     * @brief Construct a `RoutingTable`, creating one `ProcessQueue` per RIB.
     * @param cs `ControlScheduler` that owns the thread pool.
     */
    RoutingTable(ControlScheduler& cs)
        : rib4(cs.create()), rib6(cs.create())
    {}

    /**
     * @brief Destroy the routing table, clearing all routes and reclaiming RCU memory.
     */
    ~RoutingTable()
    {
        clearAll();
    }

    // ROUTE INSTALLATION

    /**
     * @brief Enqueue installation of a single route.
     *
     * Dispatches to `rib4` or `rib6` based on `AddrType`.  Takes ownership
     * of `entry`.
     *
     * @tparam AddrType `uint32_t` (IPv4) or `__uint128_t` (IPv6).
     * @param entry Heap-allocated route entry to install.
     */
    template <typename AddrType>
    void addRoute(const RibEntry<AddrType>* entry)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            rib4.addRoute(entry);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            rib6.addRoute(entry);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    /**
     * @brief Enqueue installation of a batch of routes.
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param entries Vector of heap-allocated entries; cleared on return.
     */
    template <typename AddrType>
    void addRoutes(std::vector<RibEntry<AddrType>*>& entries)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            rib4.addRoutes(entries);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            rib6.addRoutes(entries);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    // ROUTE WITHDRAWAL

    /**
     * @brief Enqueue withdrawal of a single prefix for a given source.
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param prefix Network prefix address.
     * @param length Prefix length in bits.
     * @param src    Protocol source being removed.
     * @param pid    Process instance ID.
     */
    template <typename AddrType>
    void removeRoute(AddrType prefix, uint8_t length, RouteSource src, uint32_t pid)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            rib4.removeRoute(prefix, length, src, pid);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            rib6.removeRoute(prefix, length, src, pid);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    /**
     * @brief Enqueue withdrawal of a batch of prefixes for a given source.
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param withdraws Vector of (prefix, length) pairs.
     * @param src       Protocol source being removed.
     * @param pid       Process instance ID.
     */
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

    // LOOKUP

    /**
     * @brief Longest-prefix-match lookup (byte-span form).
     *
     * @warning Caller must hold an `RCU::Guard`; the returned pointer is
     *          valid only within that guard's scope.
     *
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param addr Network-order address span.
     * @return Pointer to the best-matching `RibEntry`, or `nullptr`.
     */
    template <typename AddrType>
    RibEntry<AddrType>* lookup(const types::NetworkSpan<AddrType>& addr, utils::RCU::Guard&)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.lookup(addr);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.lookup(addr);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    /**
     * @brief Longest-prefix-match lookup (integer address form).
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param addr Destination address.
     * @return Pointer to the best-matching `RibEntry`, or `nullptr`.
     */
    template <typename AddrType>
    RibEntry<AddrType>* lookup(AddrType addr, utils::RCU::Guard& g)
    {
        return lookup<AddrType>(reinterpret_cast<const types::NetworkSpan<AddrType>&>(addr, g));
    }

    // WATCH SUBSCRIPTIONS

    /**
     * @brief Register a callback for best-route changes on an exact prefix.
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param prefix  Network prefix to watch.
     * @param length  Prefix length in bits.
     * @param ctx     Caller context pointer.
     * @param fn      Callback function.
     * @return Non-zero watch ID on success; 0 on failure.
     */
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

    /**
     * @brief Register a callback tracking the resolving route for a host address.
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param addr Host address to watch.
     * @param ctx  Caller context pointer.
     * @param fn   Callback function.
     * @return Non-zero watch ID, or 0 if unreachable.
     */
    template <typename AddrType>
    typename Rib<AddrType>::WatchId watchAddress(AddrType addr, void* ctx, typename Rib<AddrType>::Callback fn,
                                                 typename Rib<AddrType>::WatchFilter filter = {})
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            return rib4.watchAddress(addr, ctx, fn, filter);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            return rib6.watchAddress(addr, ctx, fn, filter);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
        return 0;
    }

    /**
     * @brief Register a callback for route changes from a specific protocol process.
     * @tparam AddrType `uint32_t` or `__uint128_t`.
     * @param src Protocol source to watch.
     * @param pid Process instance ID.
     * @param ctx Caller context pointer.
     * @param fn  Callback function.
     * @return Non-zero watch ID on success; 0 on failure.
     */
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

    /**
     * @brief Cancel any watch (prefix, address, or protocol) by ID.
     * @param id   Watch ID to cancel.
     * @param isV6 `true` to cancel in the IPv6 RIB; `false` for IPv4.
     */
    template <typename AddrType>
    void unwatchAddress(uint32_t id)
    {
        if constexpr (std::is_same_v<AddrType, uint32_t>)
            rib4.unwatchRoute(id);
        else if constexpr (std::is_same_v<AddrType, __uint128_t>)
            rib6.unwatchRoute(id);
        else
            static_assert(always_false<AddrType>, "Unsupported Address Type");
    }

    /**
     * @brief Remove all routes from both RIBs, synchronise RCU, and reclaim memory.
     *
     * @warning Blocks until RCU quiesces.  Must not be called from a thread
     *          currently holding an `RCU::Guard`.
     */
    void clearAll() noexcept
    {
        rib4.clear();
        rib6.clear();
        utils::RCU::synchronize();
        utils::RCU::tryReclaim();
    }

private:
    template <typename T> static constexpr bool always_false = false; ///< Helper for static_assert in unsupported-type branches.
};

} // namespace core

#endif // ROUTING_TABLE_HPP
