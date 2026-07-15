/**
 * @file Rib.hpp
 * @brief Single-address-family Routing Information Base.
 */

/**
 * @defgroup CORE_ROUTING_RIB Core Routing RIB
 * @ingroup CORE_ROUTING
 * @brief Routing Information Base: per-AF RIB buckets, entries, and route source types.
 */

#ifndef RIB_HPP
#define RIB_HPP

#include <unordered_map>
#include <ControlScheduler.h>

#include "routing/fib/Fib.hpp"
#include "RibBucket.hpp"
#include "routing/RouteWatcher.hpp"

namespace core
{

/**
 * @brief Single-address-family RIB: owns the sorted `RibBucket` map, runs
 *        best-path selection, and drives FIB updates.
 *
 * `Rib` is the central control-plane store for one address family (IPv4 or
 * IPv6).  It maps every known prefix to a `RibBucket`, which in turn holds
 * all candidate routes and the current best entry.  All mutations are
 * serialised through a `ProcessQueue` so the RIB state is always consistent
 * from the writer's perspective.  Data-plane reads go directly to the `Fib`
 * via RCU.
 *
 * ## Architectural Role
 * `RoutingTable` owns two `Rib` instances — one for IPv4 (`uint32_t`) and one
 * for IPv6 (`__uint128_t`).  Routing protocols call `addRoute()` /
 * `removeRoute()` on `RoutingTable`, which forwards to the appropriate `Rib`.
 *
 * ## Lifecycle & Ownership
 * - Constructed with a `ProcessQueue` moved in; the queue must outlive the
 *   Rib or be the Rib's own queue (as provided by `ControlScheduler::create`).
 * - Non-copyable, non-movable.
 * - `clear()` (and the destructor) post a task that deletes all buckets and
 *   erases the FIB; callers should follow with `RCU::synchronize()`.
 *
 * ## Concurrency Model
 * - Public mutating methods (`addRoute`, `removeRoute`, `clear`) post lambdas
 *   to the internal `ProcessQueue` and return immediately — they are safe to
 *   call from any thread.
 * - `lookup()` delegates to `Fib::lookup()` which is lock-free under an
 *   `RCU::Guard`; the caller is responsible for holding the guard.
 * - Watch registration (`watchRoute`, `watchAddress`, `watchProtocol`) posts
 *   to the scheduler and returns a `WatchId` immediately.
 *
 * @tparam AddrType Unsigned integral address type (`uint32_t` or `__uint128_t`).
 *
 * @see RoutingTable
 * @see RibBucket
 * @see RouteWatcher
 * @see Fib
 */
template <typename AddrType>
class Rib
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");
    static constexpr uint8_t W = sizeof(AddrType)*8; ///< Address width in bits.

    /**
     * @brief Mask the host bits of a prefix.
     * @param p Raw prefix value.
     * @param l Prefix length in bits.
     * @return Network-masked prefix.
     */
    static AddrType mask(AddrType p, uint8_t l)
    {
        if (l == 0) return 0;
        if (l >= W) return p;
        return p & (~AddrType(0) << (W - l));
    }

    std::atomic<size_t> siz{};
    std::unordered_map<PrefixKey<AddrType>, RibBucket<AddrType>*, PrefixHash<AddrType>> table; ///< Prefix-to-bucket map.
    ProcessQueue            scheduler;    ///< Serialises all RIB mutations.
    Fib<AddrType>           fib;          ///< Forwarding table updated after each best-path run.
    RouteWatcher<AddrType>  routeWatcher; ///< Subscription manager for route-change callbacks.

public:
    /// Opaque watch subscription identifier (0 = invalid).
    using WatchId     = typename RouteWatcher<AddrType>::WatchId;
    /// Optional filter applied to watch callbacks.
    using WatchFilter = typename RouteWatcher<AddrType>::WatchFilter;
    /// Callback signature invoked on route changes.
    using Callback    = typename RouteWatcher<AddrType>::Callback;

    /**
     * @brief Construct a Rib with the given serialisation queue.
     * @param s `ProcessQueue` used to serialise all state mutations.
     *          Must be created via `ControlScheduler::create()`.
     */
    Rib(ProcessQueue&& s) : scheduler(std::move(s)), routeWatcher(fib, scheduler) {}

    Rib(const Rib&)            = delete;
    Rib& operator=(const Rib&) = delete;
    Rib(Rib&&)                 = delete;
    Rib& operator=(Rib&&)      = delete;

    /**
     * @brief Destroy the RIB: posts a final `clear()` task, then blocks until
     *        it (and any other in-flight task) finishes before tearing down
     *        `routeWatcher`/`fib`/`table`.
     */
    ~Rib()
    {
        clear();
        scheduler.release();
    }

    // ROUTE INSTALLATION

    /**
     * @brief Enqueue installation of a batch of routes.
     *
     * Takes ownership of each pointer in `es`.  The actual insertion happens
     * asynchronously on the scheduler thread.
     *
     * @param es Vector of heap-allocated `RibEntry` pointers; cleared on return.
     */
    void addRoutes(std::vector<RibEntry<AddrType>*>& es)
    {
        scheduler.post([this, routes = std::move(es)]() {
            for (const auto* rt : routes)
                installRoute(rt);
            if (siz.load(std::memory_order_relaxed) != table.size())
                siz.store(table.size(), std::memory_order_release);
        });
    }

    /**
     * @brief Enqueue installation of a single route.
     *
     * Takes ownership of `e`.  The actual insertion happens asynchronously.
     *
     * @param e Heap-allocated route entry.
     */
    void addRoute(const RibEntry<AddrType>* e)
    {
        scheduler.post([this, e]() {
            installRoute(e);
            if (siz.load(std::memory_order_relaxed) != table.size())
                siz.store(table.size(), std::memory_order_release);
        });
    }

    // ROUTE WITHDRAWAL

    /**
     * @brief Enqueue withdrawal of a batch of prefixes for a given source.
     * @param withdraws Vector of (prefix, length) pairs; moved into the task.
     * @param src       Protocol source to remove.
     * @param pid       Process instance ID (0 = any).
     */
    template <types::IsIPPrefix Prefix>    
    void removeRoutes(std::vector<Prefix>& withdraws, RouteSource src, uint64_t pid = 0)
    {
        scheduler.post([this, ws = std::move(withdraws), src, pid]() {
            for (const auto& w : ws)
                withdrawRoute(w.addr, w.prefixLength, src, pid);
        });
    }

    /**
     * @brief Enqueue withdrawal of a single prefix for a given source.
     * @param prefix Network prefix address.
     * @param length Prefix length in bits.
     * @param src    Protocol source to remove.
     * @param pid    Process instance ID (default 0).
     */
    void removeRoute(AddrType prefix, uint8_t length, RouteSource src, uint64_t pid = 0)
    {
        scheduler.post([this, prefix, length, src, pid]() {
            withdrawRoute(prefix, length, src, pid);
        });
    }

    // ACCESSORS

    /**
     * @brief Return a reference to the underlying FIB (data-plane read path).
     */
    Fib<AddrType>& getFib() noexcept { return fib; }

    // WATCH SUBSCRIPTIONS

    /**
     * @brief Watch best-route changes for an exact (prefix, length).
     *
     * The callback is fired on the scheduler thread whenever the best route
     * for the prefix changes (install, update, or withdraw).  Returning `true`
     * from the callback auto-cancels the watch.
     *
     * @param prefix  Network prefix to watch.
     * @param length  Prefix length in bits.
     * @param ctx     Caller context pointer passed back in `CallbackCtx`.
     * @param fn      Callback function.
     * @param filter  Optional source/process filter.
     * @return Non-zero `WatchId` on success; 0 on failure.
     */
    WatchId watchRoute(AddrType prefix, uint8_t length, void* ctx, Callback fn, WatchFilter filter = {})
    {
        return routeWatcher.watchRoute(prefix, length, ctx, fn, filter);
    }

    /**
     * @brief Watch the resolving route for a host address (follows re-routes).
     *
     * Internally pins to the longest-matching prefix that currently resolves
     * the address; if that prefix is withdrawn, re-resolves via the FIB
     * and re-pins automatically.  Returns 0 if the address is currently
     * unreachable (after applying `filter`).
     *
     * @param addr    Host address to watch.
     * @param ctx     Caller context pointer.
     * @param fn      Callback function.
     * @param filter  Optional source/process filter.
     * @return Non-zero `WatchId`, or 0 if unreachable.
     */
    WatchId watchAddress(AddrType addr, void* ctx, Callback fn, WatchFilter filter = {})
    {
        return routeWatcher.watchAddress(addr, ctx, fn, filter);
    }

    /**
     * @brief Watch all best-route changes from a specific (source, processId).
     * @param src Protocol source to watch.
     * @param pid Process instance ID.
     * @param ctx Caller context pointer.
     * @param fn  Callback function.
     * @return Non-zero `WatchId` on success; 0 on failure.
     */
    WatchId watchProtocol(RouteSource src, uint64_t pid, void* ctx, Callback fn)
    {
        return routeWatcher.watchProtocol(src, pid, ctx, fn);
    }

    /**
     * @brief Cancel a previously registered watch by ID.
     * @param id Watch identifier returned by a `watch*` method.
     */
    void unwatchRoute(WatchId id)
    {
        routeWatcher.remove(id);
    }

    /**
     * @brief Remove all routes and clear the FIB; fires "gone" callbacks on
     *        all active watches.
     */
    void clear() noexcept
    {
        scheduler.post([this]() {
            fib.clear();

            for (auto& kv : table)
                delete kv.second;

            table.clear();

            routeWatcher.announceAllGone();
        });
    }

    /**
     * @brief Returns the amount of active routes in the RIB
     */
    size_t size() const
    {
        return siz.load(std::memory_order_relaxed);
    }

    /**
     * @brief Longest-prefix-match lookup (data-plane fast path).
     *
     * @warning The caller must hold an `RCU::Guard` for the duration of the
     *          lookup and must not dereference the returned pointer after the
     *          guard is released.
     *
     * @param addr Network-order address span.
     * @return Pointer to the best-matching `RibEntry`, or `nullptr`.
     */
    RibEntry<AddrType>* lookup(const types::NetworkSpan<AddrType>& addr) const
    {
        return fib.lookup(addr);
    }
    /**
     * TODO doxy comment
     */
    RibBucket<AddrType>* lookupBucket(PrefixKey<AddrType>& prefixKey) const
    {
        if (auto bucket = table.find(prefixKey); bucket != table.end())
            return bucket.second;
        return nullptr;
    }

    /**
     * TODO: Finish Doxy
     */
    void wait()
    {
        scheduler.waitIdle();
    }

private:
    /**
     * @brief Install a single route into the RIB (runs on the scheduler thread).
     * @param e Heap-allocated route entry; ownership is passed to the bucket.
     */
    void installRoute(const RibEntry<AddrType>* e)
    {
        PrefixKey key{ mask(e->prefix, e->length), e->length };

        auto it = table.find(key);
        if (it == table.end())
        {
            RibBucket<AddrType>* b = new RibBucket<AddrType>();
            b->addRoute(e);
            fib.insert(key.prefix, key.length, &b->fibEntry);
            table.emplace(key, b);
            routeWatcher.announceRouteChange(key.prefix, key.length, *b);
        }
        else
        {
            utils::RCU::Guard g;
            if (it->second->addRoute(e))
                routeWatcher.announceRouteChange(key.prefix, key.length, *it->second);
        }
    }

    /**
     * @brief Withdraw a route from the RIB (runs on the scheduler thread).
     * @param prefix Network prefix address.
     * @param length Prefix length in bits.
     * @param src    Protocol source being removed.
     * @param pid    Process instance ID.
     */
    void withdrawRoute(AddrType prefix, uint8_t length, RouteSource src, uint64_t pid = 0)
    {
        PrefixKey key{ mask(prefix, length), length };

        auto it = table.find(key);
        if (it == table.end()) return;

        RibBucket<AddrType>* b = it->second;
        b->removeRoute(src, pid);

        utils::RCU::Guard g;

        if (b->empty())
        {
            table.erase(it);
            fib.erase(key.prefix, key.length);
            routeWatcher.announceRouteChange(key.prefix, key.length, *b);
            auto deleter = [](void* b) { delete reinterpret_cast<RibBucket<AddrType>*>(b); };
            utils::RCU::retire(deleter, b);
        }
        else
        {
            routeWatcher.announceRouteChange(key.prefix, key.length, *b);
        }
    }
};

} // namespace core

#endif // RIB_HPP
