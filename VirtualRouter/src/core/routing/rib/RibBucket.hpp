// RibBucket.hpp

#ifndef RIB_BUCKET_HPP
#define RIB_BUCKET_HPP

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "RibEntry.hpp"
#include <RCU.hpp>

namespace core
{

template <typename AddrType>
class RibBucket
{
public:
    std::vector<RibEntry<AddrType>> routes;

    RibEntry<AddrType>* bestEntry = nullptr;
    RibEntry<AddrType>* prevBest  = nullptr;

    std::atomic<RibEntry<AddrType>*> fibEntry{nullptr};

    RibBucket() noexcept = default;

    ~RibBucket()
    {
        RibEntry<AddrType>* val = fibEntry.exchange(nullptr, std::memory_order_acq_rel);
        if (val) utils::RCU::retire([val]{ delete val; });
    }

    // Returns true if the bucket's best route changed and the FIB was updated.
    bool addRoute(const RibEntry<AddrType>* e) noexcept
    {
        bool replaced = false;

        for (RibEntry<AddrType>& r : routes)
        {
            if (r.source == e->source && r.processId == e->processId)
            {
                if (r.metric        == e->metric        &&
                    r.nextHopCount  == e->nextHopCount  &&
                    r.adminDistance == e->adminDistance)
                    return false;
                r = *e;
                replaced = true;
                break;
            }
        }

        if (!replaced)
            routes.push_back(*e);
        delete e;

        selectBest();
        return true;
    }

    void removeRoute(RouteSource src, uint64_t pid = 0) noexcept
    {
        routes.erase(
            std::remove_if(routes.begin(), routes.end(),
                [src, pid](const RibEntry<AddrType>& r)
                {
                    return r.source == src && r.processId == pid;
                }),
            routes.end());
        selectBest();
    }

    // Best route among all entries for a specific process instance.
    RibEntry<AddrType>* getBestRoute(RouteSource src, uint64_t pid) noexcept
    {
        RibEntry<AddrType>* best = nullptr;
        for (RibEntry<AddrType>& r : routes)
        {
            if (r.source == src && r.processId == pid)
            {
                if (!best ||
                    r.adminDistance < best->adminDistance ||
                    (r.adminDistance == best->adminDistance && r.metric < best->metric))
                    best = &r;
            }
        }
        return best;
    }

    // Best route among all entries for a specific process instance (any source).
    RibEntry<AddrType>* getBestRoute(uint64_t pid) noexcept
    {
        RibEntry<AddrType>* best = nullptr;
        for (RibEntry<AddrType>& r : routes)
        {
            if (r.processId == pid)
            {
                if (!best ||
                    r.adminDistance < best->adminDistance ||
                    (r.adminDistance == best->adminDistance && r.metric < best->metric))
                    best = &r;
            }
        }
        return best;
    }

    // Best route among all entries for a specific source type (any process).
    RibEntry<AddrType>* getBestRoute(RouteSource src) noexcept
    {
        RibEntry<AddrType>* best = nullptr;
        for (RibEntry<AddrType>& r : routes)
        {
            if (r.source == src)
            {
                if (!best ||
                    r.adminDistance < best->adminDistance ||
                    (r.adminDistance == best->adminDistance && r.metric < best->metric))
                    best = &r;
            }
        }
        return best;
    }

    RibEntry<AddrType>* getBestRoute() noexcept
    {
        return bestEntry;
    }

    void selectBest() noexcept
    {
        prevBest  = bestEntry;
        bestEntry = nullptr;

        for (RibEntry<AddrType>& r : routes)
        {
            if (!bestEntry ||
                r.adminDistance < bestEntry->adminDistance ||
                (r.adminDistance == bestEntry->adminDistance && r.metric < bestEntry->metric))
                bestEntry = &r;
        }

        // Always push a fresh heap copy into the FIB so RCU readers are never
        // exposed to a pointer into the (potentially reallocating) routes vector.
        RibEntry<AddrType>* copy = bestEntry ? new RibEntry<AddrType>(*bestEntry) : nullptr;
        RibEntry<AddrType>* old  = fibEntry.exchange(copy, std::memory_order_acq_rel);
        if (old) utils::RCU::retire([old]{ delete old; });
    }

    bool empty() const noexcept
    {
        return fibEntry.load(std::memory_order_acquire) == nullptr;
    }

    void clear() noexcept
    {
        RibEntry<AddrType>* old = fibEntry.exchange(nullptr, std::memory_order_acq_rel);
        if (old) utils::RCU::retire([old]{ delete old; });
        routes.clear();
        bestEntry = nullptr;
        prevBest  = nullptr;
    }
};

} // namespace core

#endif // RIB_BUCKET_HPP

