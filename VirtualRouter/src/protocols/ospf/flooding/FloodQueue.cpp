// FloodQueue.cpp

#include "FloodQueue.h"
#include "FloodTypes.hpp"

namespace OSPF
{
FloodQueue::FloodQueue(std::pmr::memory_resource* mr, size_t reserveUnique)
#if OSPF_LSDB_USE_PMR
    : q(mr), pending(mr)
#else
    : q(), pending()
#endif
{
    if (reserveUnique)
        pending.reserve(reserveUnique);
}

size_t FloodQueue::size() const noexcept
{
    return q.size();
}

bool FloodQueue::empty() const noexcept
{
    return q.empty();
}

void FloodQueue::clear()
{
    q.clear();
    pending.clear();
}

bool FloodQueue::enqueue(const FloodRequest& req)
{
    FloodRequestKey k{};
    k.key = req.key;
    k.area = req.area;
    k.incomingInterface = req.incomingInterface;
    k.excludeIncoming = static_cast<uint8_t>(req.excludeIncoming ? 1 : 0);

    auto [it, inserted] = pending.insert(k);
    if (!inserted)
        return false;

    q.push_back(req);
    return true;
}

bool FloodQueue::tryDequeue(FloodRequest& out)
{
    if (q.empty())
        return false;

    out = q.front();
    q.pop_front();

    FloodRequestKey k{};
    k.key = out.key;
    k.area = out.area;
    k.incomingInterface = out.incomingInterface;
    k.excludeIncoming = static_cast<uint8_t>(out.excludeIncoming ? 1 : 0);

    pending.erase(k);
    return true;
}
}

