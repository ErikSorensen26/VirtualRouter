// FloodQueue.hpp

#ifndef OSPF_FLOODING_FLOOD_QUEUE_HPP
#define OSPF_FLOODING_FLOOD_QUEUE_HPP

#include <deque>
#include <unordered_set>

#include <LSDB.hpp>
#include "FloodRequestKey.hpp"

namespace OSPF
{
struct FloodRequest;
struct FloodRequestKey;

class FloodQueue final
{
public:
#if OSPF_LSDB_USE_PMR
    using QueueT = std::pmr::deque<FloodRequest>;
    using SetT = std::pmr::unordered_set<FloodRequestKey>;
#else
    using QueueT = std::deque<FloodRequest>;
    using SetT = std::unordered_set<FloodRequestKey>;
#endif

    explicit FloodQueue(std::pmr::memory_resource* mr = std::pmr::get_default_resource(), size_t reserveUnique = 0);

    FloodQueue(const FloodQueue&) = delete;
    FloodQueue& operator=(const FloodQueue&) = delete;

    FloodQueue(FloodQueue&&) noexcept = delete;
    FloodQueue& operator=(FloodQueue&&) noexcept = delete;

    size_t size() const noexcept;
    bool empty() const noexcept;

    void clear();

    bool enqueue(const FloodRequest& req);
    
    bool tryDequeue(FloodRequest& out);

private:
    QueueT q;
    SetT pending;
};
}

#endif // OSPF_FLOODING_FLOOD_QUEUE_HPP
