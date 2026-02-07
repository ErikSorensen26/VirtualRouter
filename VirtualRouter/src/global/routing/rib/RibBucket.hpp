// RibBucket.hpp

#ifndef RIB_BUCKET_HPP
#define RIB_BUCKET_HPP

#include <vector>
#include <algorithm>
#include <cstdint>
#include "RibEntry.hpp"
#include <RCU.hpp>

template <typename AddrType>
class RibBucket
{
public:
    std::vector<RibEntry<AddrType>> routes;
    std::atomic<RibEntry<AddrType>*>* fibEntry = nullptr;

    RibBucket() noexcept
        : fibEntry(new std::atomic<RibEntry<AddrType>*>) {}
    ~RibBucket()
    {
        delete fibEntry;
    }

    bool addRoute(const RibEntry<AddrType>& e) noexcept
    {
        bool replaced = false;

        for (RibEntry<AddrType>& r : routes)
        {
            if (r.source == e.source && r.processId == e.processId && r.topoId == e.topoId)
            {
                if (r.metric == e.metric && r.nextHopCount == e.nextHopCount && r.adminDistance == e.adminDistance)
                    return false;
                r = e;
                replaced = true;
                break;
            }
        }

        if (!replaced)
            routes.push_back(e);

        selectBest();
        return true;
    }

    void removeRoute(RouteSource src, uint32_t pid = 0) noexcept
    {
        routes.erase(
            std::remove_if(routes.begin(), routes.end(),
                [src, pid](const RibEntry<AddrType>& r){ return r.source == src && r.processId == pid; }),
            routes.end());
        selectBest();
        return;
    }

    RibEntry<AddrType>* getBestRoute(uint32_t pid) noexcept
    {
        RibEntry<AddrType>* best = nullptr;

        for (const RibEntry<AddrType>& r : routes)
        {
            if (r.processId == pid)
            {
                if (!best)
                    best = &r;
                else if (r.adminDistance < best->adminDistance || r.metric < best->metric)
                    best = &r;
            }
        }
        return best;
    }

    RibEntry<AddrType>* getBestRoute(RouteSource src, uint32_t pid) noexcept
    {
        RibEntry<AddrType>* best = nullptr;

        for (const RibEntry<AddrType>& r : routes)
        {
            if (r.source == src && r.processId == pid)
            {
                if (!best)
                    best = &r;
                else if (r.adminDistance < best->adminDistance || r.metric < best->metric)
                    best = &r;
            }
        }
        return best;
    }

    void selectBest() noexcept
    {
        if (!fibEntry) return;

        if (routes.empty())
        {
            fibEntry->store(nullptr, std::memory_order_release);
            return;
        }

        auto bestIt = std::min_element(routes.begin(), routes.end(),
            [](const RibEntry<AddrType>& a, const RibEntry<AddrType>& b) {
                if (a.adminDistance != b.adminDistance)
                    return a.adminDistance < b.adminDistance;
                return a.metric < b.metric;
            });

        if (bestIt != routes.end())
            fibEntry->store(&(*bestIt), std::memory_order_release);
        else
            fibEntry->store(nullptr, std::memory_order_release);
    }

    bool empty() const noexcept
    {
        return routes.empty() ||
            !fibEntry ||
            (fibEntry->load(std::memory_order_acquire) == nullptr);
    }

    void clear()
    {
        if (fibEntry)
            fibEntry->store(nullptr, std::memory_order_release);
        routes.clear();
    }
};

#endif // RIB_BUCKET_HPP
