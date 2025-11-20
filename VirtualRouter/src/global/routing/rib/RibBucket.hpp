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
    RibBucket(std::atomic<RibEntry<AddrType>*>* ribEntry) noexcept
        : fibEntry(ribEntry) {}

    RibBucket* clone() const noexcept
    {
        auto* b = new RibBucket<AddrType>(fibEntry);
        b->routes = routes;
        return b;
    }

    RibBucket<AddrType>* addRoute(const RibEntry<AddrType>& e) const noexcept
    {
        RibBucket* nb = clone();
        bool replaced = false;

        for (auto& r : nb->routes)
        {
            if (r.source == e.source && r.processId == e.processId)
            {
                if (r.metric == e.metric)
                {
                    delete nb;
                    return nullptr;
                }
                r = e;
                replaced = true;
                break;
            }
        }

        if (!replaced)
            nb->routes.push_back(e);

        nb->selectBest();
        return nb;
    }

    RibBucket* removeRoute(RouteSource src, uint32_t pid = 0) const noexcept
    {
        RibBucket* nb = clone();

        nb->routes.erase(
            std::remove_if(nb->routes.begin(), nb->routes.end(),
                [src, pid](const RibEntry<AddrType>& r){ return r.source == src && r.processId == pid; }),
            nb->routes.end());
        nb->selectBest();
        return nb;
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
