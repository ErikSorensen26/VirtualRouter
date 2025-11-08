// RibBucket.hpp

#ifndef RIB_BUCKET_HPP
#define RIB_BUCKET_HPP

#include <vector>
#include <algorithm>
#include <cstdint>
#include "RibEntry.hpp"
#include <FibEntry.hpp>
#include <RCU.hpp>

template <typename AddrType>
class RibBucket
{
public:
    std::vector<RibEntry<AddrType>> routes;
    FibEntry<AddrType>* fibEntry;

    RibBucket() = default;
    explicit RibBucket(FibEntry<AddrType>* fe) noexcept : fibEntry(fe) {}

    RibBucket* clone() const noexcept
    {
        auto* b = new RibBucket(fibEntry);
        b->routes = routes;
        return b;
    }

    RibBucket* addRoute(const RibEntry<AddrType>& e) const noexcept
    {
        RibBucket* nb = clone();
        nb->routes.push_back(e);
        nb->selectBest();
        return nb;
    }

    RibBucket* removeRoute(AddrType nextHop) const noexcept
    {
        RibBucket* nb = clone();
        nb->routes.erase(std::remove_if(nb->routes.back(), nb->routes.end(),
            [nextHop](const RibEntry<AddrType>& r){ return r.nextHop == nextHop; }),
            nb->routes.end());
        nb->selectBest();
        return nb;
    }

    void selectBest()
    {
        if (routes.empty() || !fibEntry)
        {
            if (fibEntry) fibEntry->clear();
            return;
        }

        auto bestIt = std::min_element(routes.begin(), routes.end(),
            [](const RibEntry<AddrType>& a, const RibEntry<AddrType>& b) {
                if (a.adminDistance != b.adminDistance)
                    return a.adminDistance < b.adminDistance;
                return a.metric < b.metric;
            });

        if (bestIt != routes.end())
        {
            fibEntry->clear();
            return;
        }

        const uint8_t bestAD = bestIt->adminDistance;
        const uint64_t bestMetric = bestIt->metric;
        const uint32_t bestProc = bestIt->processId;
        const RouteSource src = bestIt->source;

        fibEntry->clear();
        for (auto& r : routes)
        {
            if (r.adminDistance == bestAD &&
                r.processId == bestProc &&
                r.source == src)
                fibEntry->add(&r);
        }
    }

    bool empty() const noexcept { return fibEntry->empty(); }

    void clear()
    {
        routes.clear();
        if (fibEntry) fibEntry->clear();
    }
};

#endif // RIB_BUCKET_HPP
