// Rib.hpp

#ifndef RIB_HPP
#define RIB_HPP

#include <unordered_map>
#include <ControlScheduler.h>

#include "routing/fib/Fib.hpp"
#include "RibBucket.hpp"
#include "routing/RouteWatcher.hpp"

template <typename AddrType>
class Rib
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");
    static constexpr uint8_t W = sizeof(AddrType)*8;

    static AddrType mask(AddrType p, uint8_t l)
    {
        if (l == 0) return 0;
        if (l >= W) return p;
        return p & (~AddrType(0) << (W - l));
    }

    std::unordered_map<PrefixKey<AddrType>, RibBucket<AddrType>*, PrefixHash<AddrType>> table;
    Fib<AddrType> fib;
    ProcessQueue scheduler;
    RouteWatcher<AddrType> routeWatcher;

public:
    using WatchId     = typename RouteWatcher<AddrType>::WatchId;
    using WatchFilter = typename RouteWatcher<AddrType>::WatchFilter;
    using Callback    = typename RouteWatcher<AddrType>::Callback;

    Rib(ProcessQueue&& s) : scheduler(std::move(s)), routeWatcher(fib, scheduler) {}

    Rib(const Rib&) = delete;
    Rib& operator=(const Rib&) = delete;
    Rib(Rib&&) = delete;
    Rib& operator=(Rib&&) = delete;

    ~Rib() { clear(); }

    void addRoutes(std::vector<RibEntry<AddrType>*>& es)
    {
        scheduler.post([this, routes = std::move(es)]() {
            for (const auto* rt : routes)
                installRoute(rt);
        });
    }

    void addRoute(const RibEntry<AddrType>* e)
    {
        scheduler.post([this, e]() {
            installRoute(e);
        });
    }

    void removeRoutes(std::vector<std::pair<AddrType, uint8_t>>& withdraws, RouteSource src, uint64_t pid = 0)
    {
        scheduler.post([this, ws = std::move(withdraws), src, pid]() {
            for (const auto& w : ws)
                withdrawRoute(w.first, w.second, src, pid);
        });
    }

    void removeRoute(AddrType prefix, uint8_t length, RouteSource src, uint64_t pid = 0)
    {
        scheduler.post([this, prefix, length, src, pid]() {
            withdrawRoute(prefix, length, src, pid);
        });
    }

    Fib<AddrType>& getFib() noexcept { return fib; }

    WatchId watchRoute(AddrType prefix, uint8_t length, void* ctx, Callback fn, WatchFilter filter = {})
    {
        return routeWatcher.watchRoute(prefix, length, ctx, fn, filter);
    }

    WatchId watchAddress(AddrType addr, void* ctx, Callback fn, WatchFilter filter = {})
    {
        return routeWatcher.watchAddress(addr, ctx, fn, filter);
    }

    WatchId watchProtocol(RouteSource src, uint64_t pid, void* ctx, Callback fn)
    {
        return routeWatcher.watchProtocol(src, pid, ctx, fn);
    }

    void unwatchRoute(WatchId id)
    {
        routeWatcher.remove(id);
    }

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

    RibEntry<AddrType>* lookup(const uint8_t* addr) const
    {
        return fib.lookup(addr);
    }

    RibEntry<AddrType>* lookup(AddrType addr) const
    {
        return fib.lookup(addr);
    }

private:
    void installRoute(const RibEntry<AddrType>* e)
    {
        PrefixKey key{ mask(e->prefix, e->length), e->length };

        auto it = table.find(key);
        if (it == table.end())
        {
            RibBucket<AddrType>* b = new RibBucket<AddrType>();
            b->addRoute(*e);
            fib.insert(key.prefix, key.length, &b->fibEntry);
            table.emplace(key, b);
            routeWatcher.announceRouteChange(key.prefix, key.length, *b);
        }
        else
        {
            RCU::Guard g;
            if (it->second->addRoute(*e))
                routeWatcher.announceRouteChange(key.prefix, key.length, *it->second);
        }
    }

    void withdrawRoute(AddrType prefix, uint8_t length, RouteSource src, uint64_t pid = 0)
    {
        PrefixKey key{ mask(prefix, length), length };

        auto it = table.find(key);
        if (it == table.end()) return;

        RibBucket<AddrType>* b = it->second;
        b->removeRoute(src, pid);

        RCU::Guard g;

        if (b->empty())
        {
            table.erase(it);
            fib.erase(key.prefix, key.length);
            routeWatcher.announceRouteChange(key.prefix, key.length, *b);
            RCU::retire([b]{ delete b; });
        }
        else
        {
            routeWatcher.announceRouteChange(key.prefix, key.length, *b);
        }
    }
};

#endif // RIB_HPP
