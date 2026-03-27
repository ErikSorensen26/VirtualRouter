/**
 * @file RouteWatcher.hpp
 * @brief RIB subscription engine for per-prefix, per-address, and per-protocol
 *        route-change callbacks.
 */

#ifndef ROUTE_WATCHER_HPP
#define ROUTE_WATCHER_HPP

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <ControlScheduler.h>
#include <AtomicStack.hpp>

#include "fib/Fib.hpp"
#include "rib/RibBucket.hpp"
#include "rib/RouteSource.hpp"

namespace core
{

template <typename Addr>
class Rib;

/**
 * @brief Hash map key combining a `RouteSource` and a process ID.
 *
 * Used as the key for protocol-level watch subscriptions so that callbacks
 * can be scoped to exactly one routing protocol process instance.
 */
struct SrcPidKey
{
    RouteSource source;    ///< Protocol that installed the routes.
    uint64_t    processId; ///< Process instance identifier.

    bool operator==(const SrcPidKey& o) const noexcept
    {
        return source == o.source && processId == o.processId;
    }
};

/**
 * @brief Hash functor for `SrcPidKey`.
 * @ingroup CORE_ROUTING
 */
struct SrcPidHash
{
    /**
     * @brief Compute a hash value for a `SrcPidKey`.
     * @ingroup CORE_ROUTING
     * @param k Key to hash.
     * @return Hash value.
     */
    size_t operator()(const SrcPidKey& k) const noexcept
    {
        uint64_t h = static_cast<uint64_t>(k.source);
        h ^= k.processId + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

/**
 * @brief Subscription manager that fires callbacks whenever the best route for
 *        a prefix, host address, or protocol changes.
 *
 * `RouteWatcher` is the notification backbone used by protocols such as BGP
 * (network command, next-hop tracking) to react to RIB changes without polling.
 * It supports three subscription modes:
 *
 * - **Prefix watch** (`watchRoute`): fires when the best route for an exact
 *   (prefix, length) changes.
 * - **Address watch** (`watchAddress`): similar to a prefix watch but
 *   automatically follows when the resolving prefix is withdrawn and a
 *   less-specific route takes over.
 * - **Protocol watch** (`watchProtocol`): fires for any best-route change
 *   attributed to a specific (RouteSource, processId) pair.
 *
 * ## Architectural Role
 * Owned by `Rib<AddrType>` and driven exclusively by `Rib::installRoute()` /
 * `Rib::withdrawRoute()` via the `friend` declaration.  The public `watch*` /
 * `remove` methods may be called from any thread; they post work to the Rib's
 * scheduler.
 *
 * ## Lifecycle & Ownership
 * Created alongside the owning `Rib`.  All internal state is managed on the
 * scheduler thread.  On `Rib::clear()`, `announceAllGone()` fires every
 * remaining callback with `newBest = nullptr` before clearing subscription
 * tables.
 *
 * ## Concurrency Model
 * - All internal maps are accessed only on the `ProcessQueue` scheduler thread.
 * - `watchRoute`, `watchAddress`, `watchProtocol`, and `remove` are thread-safe
 *   (they post tasks) and return immediately.
 * - Watch IDs are allocated from a lock-free atomic counter + recycle stack.
 *
 * @tparam Addr Unsigned integral address type (`uint32_t` / `__uint128_t`).
 *
 * @see Rib
 * @see RibBucket
 */
template <typename Addr>
class RouteWatcher
{
    static_assert(std::is_unsigned_v<Addr>);
    static constexpr uint8_t W = sizeof(Addr) * 8; ///< Address width in bits.

public:
    /// Opaque subscription handle; 0 is the invalid/null value.
    using WatchId  = uint32_t;

    /**
     * @brief Context structure passed to every watch callback invocation.
     * @ingroup CORE_ROUTING
     */
    struct CallbackCtx
    {
        void*                  ctx;     ///< Caller-supplied context pointer.
        WatchId                id;      ///< Watch ID that triggered this invocation.
        const RibEntry<Addr>*  oldBest; ///< Previous best route (may be `nullptr`).
        const RibEntry<Addr>*  newBest; ///< New best route (may be `nullptr` = withdrawn).
    };

    /**
     * @brief Callback signature.
     * @ingroup CORE_ROUTING
     *
     * Return `true` to auto-cancel the watch after this invocation;
     * return `false` to keep the watch active.
     */
    using Callback = bool (*)(CallbackCtx&);

    /**
     * @brief Optional filter applied before invoking a watch callback.
     * @ingroup CORE_ROUTING
     *
     * When set, only routes matching the specified source and/or processId
     * are considered for the "best" value passed to the callback.
     */
    struct WatchFilter
    {
        std::optional<RouteSource> src;       ///< Restrict to this protocol source.
        std::optional<uint64_t>    processId; ///< Restrict to this process instance.
    };

private:
    /// Internal subscription node for a single watch registration.
    struct WatchNode
    {
        WatchId               id        = 0;       ///< Unique watch identifier.
        void*                 ctx       = nullptr; ///< Caller context pointer.
        Callback              fn        = nullptr; ///< Callback function.
        WatchFilter           filter;              ///< Source/process filter.
        const RibEntry<Addr>* lastKnown = nullptr; ///< Last route seen by this watcher; used for change detection.
        bool                  canceled  = false;   ///< Set by `remove()`; pruned on next fire.
    };

    /// Internal state for an address-level watch, which wraps a prefix watch.
    struct AddrWatchState
    {
        RouteWatcher*         self;          ///< Owning watcher.
        Addr                  addr;          ///< Host address being watched.
        void*                 userCtx;       ///< Original caller context.
        Callback              userFn;        ///< Original caller callback.
        WatchFilter           filter;        ///< Source/process filter.
        WatchId               addrId;        ///< ID of this address watch.
        WatchId               prefixId;      ///< ID of the underlying prefix watch currently pinned.
        const RibEntry<Addr>* lastUserRoute; ///< Last route delivered to the user callback.
    };

    std::unordered_map<PrefixKey<Addr>, std::vector<WatchNode>, PrefixHash<Addr>> prefixWatchTable; ///< Prefix-to-watchers map.
    std::unordered_map<WatchId, PrefixKey<Addr>>                                  prefixIdMap;      ///< WatchId to prefix key reverse-index.

    std::unordered_map<SrcPidKey, std::vector<WatchNode>, SrcPidHash> protocolWatchTable; ///< Protocol-to-watchers map.
    std::unordered_map<WatchId, SrcPidKey>                            protocolIdMap;      ///< WatchId to SrcPidKey reverse-index.

    std::unordered_map<WatchId, AddrWatchState> addrWatches; ///< Active address-level watches.

    Fib<Addr>&             fib;           ///< FIB reference used for re-resolution in address watches.
    ProcessQueueRef        scheduler;     ///< Scheduler queue for serialising callback delivery.
    std::atomic<WatchId>   nextId{1};     ///< Monotone counter for new watch IDs.
    types::AtomicStack<uint32_t> availableIds; ///< Recycled watch IDs.

    /**
     * @brief Mask the host bits of an address prefix.
     * @param p Raw prefix value.
     * @param l Prefix length in bits.
     * @return Network-masked address.
     */
    static Addr maskAddr(Addr p, uint8_t l) noexcept
    {
        if (l == 0) return Addr(0);
        if (l >= W) return p;
        return p & (~Addr(0) << (W - l));
    }

    /**
     * @brief Compare two `RibEntry` pointers for semantic equality.
     * @param a First entry (may be `nullptr`).
     * @param b Second entry (may be `nullptr`).
     * @return `true` if both are null, or if all forwarding-relevant fields match.
     */
    static bool routesEqual(const RibEntry<Addr>* a, const RibEntry<Addr>* b) noexcept
    {
        if (!a && !b) return true;
        if (!a || !b) return false;
        if (a->adminDistance != b->adminDistance) return false;
        if (a->metric        != b->metric)        return false;
        if (a->source        != b->source)        return false;
        if (a->processId     != b->processId)     return false;
        if (a->nextHopCount  != b->nextHopCount)  return false;
        for (uint8_t i = 0; i < a->nextHopCount; ++i)
        {
            if (a->nextHops[i].iface   != b->nextHops[i].iface)   return false;
            if (a->nextHops[i].weight  != b->nextHops[i].weight)  return false;
            if (a->nextHops[i].nextHop != b->nextHops[i].nextHop) return false;
        }
        return true;
    }

    /**
     * @brief Compute the filtered best route from a bucket for a watch node.
     * @param bucket RibBucket containing candidate routes.
     * @param filter Source/process filter to apply.
     * @return Best matching `RibEntry*`, or `nullptr`.
     */
    static RibEntry<Addr>* computeBestForPrefix(RibBucket<Addr>& bucket, const WatchFilter& filter) noexcept
    {
        if (filter.src.has_value() && filter.processId.has_value())
            return bucket.getBestRoute(*filter.src, *filter.processId);
        if (filter.src.has_value())
            return bucket.getBestRoute(*filter.src);
        if (filter.processId.has_value())
            return bucket.getBestRoute(*filter.processId);
        return bucket.bestEntry;
    }

    /**
     * @brief Apply a source/process filter to a single route.
     * @param r      Route to test (may be `nullptr`).
     * @param filter Filter to apply.
     * @return `r` if it passes the filter; `nullptr` otherwise.
     */
    static const RibEntry<Addr>* applyFilter(const RibEntry<Addr>* r, const WatchFilter& filter) noexcept
    {
        if (!r)                                                                  return nullptr;
        if (filter.src.has_value()       && r->source    != *filter.src)        return nullptr;
        if (filter.processId.has_value() && r->processId != *filter.processId)  return nullptr;
        return r;
    }

    /**
     * @brief Allocate a unique watch ID, reusing recycled IDs when available.
     * @return New watch ID (never 0).
     */
    WatchId allocateId() noexcept
    {
        uint32_t id{};
        if (availableIds.pop(id))
            return id;
        else return nextId.fetch_add(1, std::memory_order_release) ;
    }

    /**
     * @brief Iterate a list of `WatchNode`s, fire callbacks on changed routes,
     *        and compact out canceled or self-removing nodes.
     *
     * @tparam GetCur  Callable `(WatchNode&) -> const RibEntry<Addr>*` that
     *                 computes the current best for a node.
     * @tparam IdMap   Type of the reverse-index map (`prefixIdMap` or
     *                 `protocolIdMap`).
     * @param nodes   Node list to iterate (modified in place).
     * @param getCur  Best-route accessor for the current event.
     * @param idmap   Reverse-index map to remove canceled entries from.
     */
    template <typename GetCur, typename IdMap>
    void fireNodes(std::vector<WatchNode>& nodes, GetCur&& getCur, IdMap& idmap)
    {
        size_t w = 0;
        for (size_t r = 0; r < nodes.size(); ++r)
        {
            WatchNode& node = nodes[r];

            if (node.canceled)
                continue;

            const RibEntry<Addr>* cur = getCur(node);
            const RibEntry<Addr>* old = node.lastKnown;

            if (!routesEqual(old, cur))
            {
                node.lastKnown = cur;
                CallbackCtx cctx{node.ctx, node.id, old, cur};
                if (node.fn(cctx))
                {
                    availableIds.push(node.id);
                    idmap.erase(node.id);
                    continue;
                }
            }

            if (w != r) nodes[w] = std::move(nodes[r]);
            ++w;
        }
        nodes.resize(w);
    }

    /**
     * @brief Internal callback used as the underlying prefix watch for every
     *        address watch.  Handles prefix withdrawals by re-resolving via the
     *        FIB and re-pinning to the new resolving prefix.
     * @param cctx Callback context from the prefix watch.
     * @return `true` if the address watch should be removed.
     */
    static bool addrWatchCallback(CallbackCtx& cctx)
    {
        auto*  state          = static_cast<AddrWatchState*>(cctx.ctx);
        RouteWatcher* self    = state->self;
        const RibEntry<Addr>* old = state->lastUserRoute;
        const RibEntry<Addr>* cur;
        bool prefixWithdrawn = (cctx.newBest == nullptr);

        if (!prefixWithdrawn)
        {
            cur = applyFilter(cctx.newBest, state->filter);
        }
        else
        {
            // Prefix was withdrawn — re-resolve to find a less-specific
            const RibEntry<Addr>* raw = self->fib.lookup(state->addr);
            cur = applyFilter(raw, state->filter);
        }

        bool removeAddrWatch = false;

        if (!routesEqual(old, cur))
        {
            state->lastUserRoute = cur;
            CallbackCtx ucctx{state->userCtx, state->addrId, old, cur};
            if (state->userFn(ucctx))
                removeAddrWatch = true;
        }

        if (prefixWithdrawn)
        {
            if (!removeAddrWatch && cur)
            {
                // Re-pin to the new resolving prefix
                WatchId newId = self->watchRoute(cur->prefix, cur->length, state, addrWatchCallback);
                state->prefixId = newId;
            }
            else
            {
                removeAddrWatch = true;
            }
        }

        if (removeAddrWatch)
        {
            WatchId addrId = state->addrId;
            self->availableIds.push(addrId);
            self->addrWatches.erase(addrId); // state is dangling after this
            return true;
        }

        // Unsubscribe old prefix watch if we just re-pinned to a new one
        return prefixWithdrawn;
    }

public:
    /**
     * @brief Construct a `RouteWatcher` bound to the given FIB and scheduler.
     * @param f  FIB used for address-watch re-resolution.
     * @param sc Scheduler queue; all callbacks are delivered on this thread.
     */
    explicit RouteWatcher(Fib<Addr>& f, ProcessQueue& sc) : fib(f), scheduler(sc.ref()) {}

    /**
     * @brief Destructor — all active watches are dropped without firing callbacks.
     */
    ~RouteWatcher() = default;

    RouteWatcher(const RouteWatcher&)            = delete;
    RouteWatcher& operator=(const RouteWatcher&) = delete;

    // WATCH REGISTRATION

    /**
     * @brief Register a callback for best-route changes on an exact prefix.
     *
     * The callback fires each time the best route (as filtered by `filter`)
     * for `(prefix, length)` changes.  Returning `true` from the callback
     * cancels the watch.
     *
     * @param prefix  Network prefix to watch.
     * @param length  Prefix length in bits.
     * @param ctx     Opaque context pointer passed back in `CallbackCtx`.
     * @param fn      Callback; must not be null.
     * @param filter  Optional source/process filter.
     * @return Non-zero `WatchId`, or 0 on failure.
     */
    WatchId watchRoute(Addr prefix, uint8_t length, void* ctx, Callback fn,
                       WatchFilter filter = {})
    {
        if (!fn) return 0;
        WatchId id = allocateId();
        if (!id) return 0;
        PrefixKey<Addr> key{maskAddr(prefix, length), length};
        scheduler.post([this, key, id, ctx, fn, filter]() {
            prefixIdMap.emplace(id, key);
            prefixWatchTable[key].push_back(WatchNode{id, ctx, fn, filter});
        });
        return id;
    }

    /**
     * @brief Register a callback tracking the resolving route for a host address.
     *
     * Returns 0 immediately if the address is currently unreachable (or does
     * not pass `filter`).  Once registered, the watch follows prefix changes
     * transparently.
     *
     * @param addr    Host address to watch.
     * @param ctx     Opaque context pointer.
     * @param fn      Callback; must not be null.
     * @param filter  Optional source/process filter.
     * @return Non-zero `WatchId`, or 0 if unreachable.
     */
    WatchId watchAddress(Addr addr, void* ctx, Callback fn, WatchFilter filter = {})
    {
        if (!fn) return 0;

        // Quick reachability check before allocating an ID.
        {
            utils::RCU::Guard g;
            if (!applyFilter(fib.lookup(addr), filter)) return 0;
        }

        WatchId addrId = allocateId();
        if (!addrId) return 0;

        scheduler.post([this, addr, addrId, ctx, fn, filter]() {
            utils::RCU::Guard g;
            const RibEntry<Addr>* raw = fib.lookup(addr);
            const RibEntry<Addr>* cur = applyFilter(raw, filter);

            if (!cur)
            {
                availableIds.push(addrId);
                return;
            }

            auto [it, ok] = addrWatches.emplace(addrId,
                AddrWatchState{this, addr, ctx, fn, filter, addrId, 0, cur});

            AddrWatchState* state = &it->second;
            state->prefixId = watchRoute(cur->prefix, cur->length, state, addrWatchCallback);
        });
        return addrId;
    }

    /**
     * @brief Register a callback for best-route changes from a specific protocol
     *        process across all prefixes.
     * @param src Protocol source to watch.
     * @param pid Process instance ID.
     * @param ctx Opaque context pointer.
     * @param fn  Callback; must not be null.
     * @return Non-zero `WatchId`, or 0 on failure.
     */
    WatchId watchProtocol(RouteSource src, uint64_t pid, void* ctx, Callback fn)
    {
        if (!fn) return 0;
        WatchId id = allocateId();
        if (!id) return 0;
        SrcPidKey key{src, pid};
        scheduler.post([this, id, key, ctx, fn]() {
            protocolIdMap.emplace(id, key);
            protocolWatchTable[key].push_back(WatchNode{id, ctx, fn});
        });
        return id;
    }

    /**
     * @brief Cancel a watch subscription.
     *
     * Safe to call from any thread.  The cancellation is posted to the
     * scheduler and takes effect asynchronously.
     *
     * @param id Watch ID to cancel.
     */
    void remove(WatchId id) noexcept
    {
        scheduler.post([this, id]() {
            {
                auto it = prefixIdMap.find(id);
                if (it != prefixIdMap.end())
                {
                    auto tv = prefixWatchTable.find(it->second);
                    if (tv != prefixWatchTable.end())
                        for (WatchNode& n : tv->second)
                            if (n.id == id) { n.canceled = true; break; }
                    availableIds.push(it->first);
                    prefixIdMap.erase(it);
                    return;
                }
            }
            {
                auto it = protocolIdMap.find(id);
                if (it != protocolIdMap.end())
                {
                    auto tv = protocolWatchTable.find(it->second);
                    if (tv != protocolWatchTable.end())
                        for (WatchNode& n : tv->second)
                            if (n.id == id) { n.canceled = true; break; }
                    availableIds.push(it->first);
                    protocolIdMap.erase(it);
                    return;
                }
            }
            {
                auto it = addrWatches.find(id);
                if (it == addrWatches.end()) return;
                if (it->second.prefixId)
                    remove(it->second.prefixId); // cancel the pinned prefix watch
                availableIds.push(it->first);
                addrWatches.erase(it);
                return;
            }
        });
    }

private:
    friend Rib<Addr>;

    /**
     * @brief Called by `Rib` after every `addRoute` / `removeRoute` that
     *        touches `bucket`.  Fires all matching watches whose observed
     *        route has changed.
     * @param prefix Network prefix that changed.
     * @param length Prefix length in bits.
     * @param bucket Bucket whose best-path was just recomputed.
     */
    void announceRouteChange(Addr prefix, uint8_t length, RibBucket<Addr>& bucket)
    {
        // Prefix watches (includes addr watch proxy nodes)
        if (bucket.bestEntry != bucket.prevBest)
        {
            PrefixKey<Addr> key{maskAddr(prefix, length), length};
            auto it = prefixWatchTable.find(key);
            if (it != prefixWatchTable.end())
            {
                fireNodes(it->second,
                    [&](WatchNode& n){ return computeBestForPrefix(bucket, n.filter); },
                    prefixIdMap);
                if (it->second.empty())
                    prefixWatchTable.erase(it);
            }
        }

        // Protocol watches
        if (bucket.routes.empty())
        {
            for (auto pit = protocolWatchTable.begin(); pit != protocolWatchTable.end(); )
            {
                fireNodes(pit->second,
                    [](WatchNode&){ return static_cast<RibEntry<Addr>*>(nullptr); },
                    protocolIdMap);
                pit = pit->second.empty() ? protocolWatchTable.erase(pit) : ++pit;
            }
        }
        else
        {
            // Fire old source's watchers (route may have been displaced or changed).
            if (bucket.prevBest)
            {
                SrcPidKey spk{bucket.prevBest->source, bucket.prevBest->processId};
                auto pit = protocolWatchTable.find(spk);
                if (pit != protocolWatchTable.end())
                {
                    fireNodes(pit->second,
                        [&](WatchNode&){ return bucket.getBestRoute(spk.source, spk.processId); },
                        protocolIdMap);
                    if (pit->second.empty()) protocolWatchTable.erase(pit);
                }
            }

            // Fire new best's source watchers — covers first-install and source changes.
            if (bucket.bestEntry)
            {
                SrcPidKey spk{bucket.bestEntry->source, bucket.bestEntry->processId};
                bool alreadyFired = bucket.prevBest &&
                                    bucket.prevBest->source    == spk.source &&
                                    bucket.prevBest->processId == spk.processId;
                if (!alreadyFired)
                {
                    auto pit = protocolWatchTable.find(spk);
                    if (pit != protocolWatchTable.end())
                    {
                        fireNodes(pit->second,
                            [&](WatchNode&){ return bucket.getBestRoute(spk.source, spk.processId); },
                            protocolIdMap);
                        if (pit->second.empty()) protocolWatchTable.erase(pit);
                    }
                }
            }
        }
    }

    /**
     * @brief Fire every active watch with `newBest = nullptr` and clear all
     *        subscription tables.  Called by `Rib::clear()`.
     */
    void announceAllGone()
    {
        auto fireAll = [&](auto& t, auto& idm)
        {
            for (auto& [_, nodes] : t)
            {
                for (WatchNode& node : nodes)
                {
                    availableIds.push(node.id);
                    if (node.canceled || !node.lastKnown) continue;
                    const RibEntry<Addr>* old = node.lastKnown;
                    node.lastKnown = nullptr;
                    CallbackCtx cctx{node.ctx, node.id, old, nullptr};
                    node.fn(cctx);
                }
            }
            t.clear();
            idm.clear();
        };

        fireAll(prefixWatchTable,   prefixIdMap);
        fireAll(protocolWatchTable, protocolIdMap);

        for (auto& [id, state] : addrWatches)
        {
            availableIds.push(id);
            if (state.lastUserRoute)
            {
                CallbackCtx cctx{state.userCtx, id, state.lastUserRoute, nullptr};
                state.userFn(cctx);
            }
        }
        addrWatches.clear();
    }
};

} // namespace core

#endif // ROUTE_WATCHER_HPP
