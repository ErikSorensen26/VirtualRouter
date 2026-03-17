// RouteWatcher.hpp

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

template <typename Addr>
class Rib;

struct SrcPidKey
{
    RouteSource source;
    uint64_t    processId;

    bool operator==(const SrcPidKey& o) const noexcept
    {
        return source == o.source && processId == o.processId;
    }
};

struct SrcPidHash
{
    size_t operator()(const SrcPidKey& k) const noexcept
    {
        uint64_t h = static_cast<uint64_t>(k.source);
        h ^= k.processId + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

template <typename Addr>
class RouteWatcher
{
    static_assert(std::is_unsigned_v<Addr>);
    static constexpr uint8_t W = sizeof(Addr) * 8;

public:
    using WatchId  = uint32_t; // 0 = invalid

    struct CallbackCtx
    {
        void*                  ctx;
        WatchId                id;
        const RibEntry<Addr>*  oldBest;
        const RibEntry<Addr>*  newBest;
    };

    using Callback = bool (*)(CallbackCtx&);

    struct WatchFilter
    {
        std::optional<RouteSource> src;
        std::optional<uint64_t>    processId;
    };

private:
    struct WatchNode
    {
        WatchId               id        = 0;
        void*                 ctx       = nullptr;
        Callback              fn        = nullptr;
        WatchFilter           filter;
        const RibEntry<Addr>* lastKnown = nullptr;
        bool                  canceled  = false;
    };

    struct AddrWatchState
    {
        RouteWatcher*         self;
        Addr                  addr;
        void*                 userCtx;
        Callback              userFn;
        WatchFilter           filter;
        WatchId               addrId;
        WatchId               prefixId;
        const RibEntry<Addr>* lastUserRoute;
    };

    std::unordered_map<PrefixKey<Addr>, std::vector<WatchNode>, PrefixHash<Addr>> prefixWatchTable;
    std::unordered_map<WatchId, PrefixKey<Addr>>                                  prefixIdMap;

    std::unordered_map<SrcPidKey, std::vector<WatchNode>, SrcPidHash> protocolWatchTable;
    std::unordered_map<WatchId, SrcPidKey>                            protocolIdMap;

    std::unordered_map<WatchId, AddrWatchState> addrWatches;

    Fib<Addr>& fib;
    ProcessQueueRef scheduler;
    std::atomic<WatchId> nextId{1};
    AtomicStack<uint32_t> availableIds;

    static Addr maskAddr(Addr p, uint8_t l) noexcept
    {
        if (l == 0) return Addr(0);
        if (l >= W) return p;
        return p & (~Addr(0) << (W - l));
    }

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

    static const RibEntry<Addr>* applyFilter(const RibEntry<Addr>* r, const WatchFilter& filter) noexcept
    {
        if (!r)                                                                  return nullptr;
        if (filter.src.has_value()       && r->source    != *filter.src)        return nullptr;
        if (filter.processId.has_value() && r->processId != *filter.processId)  return nullptr;
        return r;
    }

    WatchId allocateId() noexcept
    {
        uint32_t id{};
        if (availableIds.pop(id))
            return id;
        else return nextId.fetch_add(1, std::memory_order_release) ;
    }

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
            CallbackCtx ucctx{state->userCtx, old, cur};
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
    explicit RouteWatcher(Fib<Addr>& f, ProcessQueue& sc) : fib(f), scheduler(sc.ref()) {}
    ~RouteWatcher() = default;

    RouteWatcher(const RouteWatcher&)            = delete;
    RouteWatcher& operator=(const RouteWatcher&) = delete;

    // Watch best-route changes for an exact (prefix, length).
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

    WatchId watchAddress(Addr addr, void* ctx, Callback fn, WatchFilter filter = {})
    {
        if (!fn) return 0;

        // Quick reachability check before allocating an ID.
        {
            RCU::Guard g;
            if (!applyFilter(fib.lookup(addr), filter)) return 0;
        }

        WatchId addrId = allocateId();
        if (!addrId) return 0;

        scheduler.post([this, addr, addrId, ctx, fn, filter]() {
            RCU::Guard g;
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

    // Watch best-route changes from a specific (src, pid) across all prefixes.
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
                {
                    availableIds.push(it->first);
                    prefixWatchTable.erase(it);
                }
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

#endif // ROUTE_WATCHER_HPP
