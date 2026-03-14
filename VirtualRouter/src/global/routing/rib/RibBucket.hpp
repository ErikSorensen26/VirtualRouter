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
        : fibEntry(new std::atomic<RibEntry<AddrType>*>(nullptr)) {}
    ~RibBucket()
    {
        RibEntry<AddrType>* val = fibEntry->exchange(nullptr, std::memory_order_acq_rel);
        RCU::retire([val]{ delete val; });
        delete fibEntry;
    }

    bool addRoute(const RibEntry<AddrType>& e) noexcept
    {
        bool replaced = false;

        for (RibEntry<AddrType>& r : routes)
        {
            if (r.source == e.source && r.processId == e.processId)
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

    RibEntry<AddrType>* getBestRoute(RouteSource src, uint32_t pid) noexcept
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

    void selectBest() noexcept
    {
        if (!fibEntry) return;

        RibEntry<AddrType>* newBest = nullptr;

        if (!routes.empty())
        {
            auto bestIt = std::min_element(routes.begin(), routes.end(),
                [](const RibEntry<AddrType>& a, const RibEntry<AddrType>& b) {
                    if (a.adminDistance != b.adminDistance)
                        return a.adminDistance < b.adminDistance;
                    return a.metric < b.metric;
                });

            if (bestIt != routes.end())
                newBest = new RibEntry<AddrType>(*bestIt);
        }

        RibEntry<AddrType>* old = fibEntry->exchange(newBest, std::memory_order_acq_rel);
        RCU::retire([old]{ delete old; });
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
        {
            RibEntry<AddrType>* old = fibEntry->exchange(nullptr, std::memory_order_acq_rel);
            RCU::retire([old]{ delete old; });
        }
        routes.clear();
    }
};

#endif // RIB_BUCKET_HPP
