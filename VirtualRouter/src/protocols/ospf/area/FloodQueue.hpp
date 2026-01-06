// FloodQueue.hpp

#ifndef OSPF_FLOOD_QUEUE_HPP
#define OSPF_FLOOD_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

#include <LSDB.hpp>

namespace OSPF
{
class FloodQueue
{
public:
    explicit FloodQueue(size_t capacity)
        : cap(capacity),
          slots(cap)
    {
        for (size_t i = 0; i < cap; ++i)
            slots[i].seq.store(i, std::memory_order_relaxed);
    }

    FloodQueue(const FloodQueue&) = delete;
    FloodQueue& operator=(const FloodQueue&) = delete;
    FloodQueue(FloodQueue&&) noexcept = delete;
    FloodQueue& operator=(FloodQueue&&) noexcept = delete;

    size_t size() const noexcept
    {
        return count.load(std::memory_order_acquire);
    }

    bool empty() const noexcept
    {
        return size() == 0;
    }

    bool enqueue(const LsaRecordRef& req) { return enqueueImpl(req); }
    bool enqueue(LsaRecordRef&& req) { return enqueueImpl(std::move(req)); }

    std::vector<LsaRecordRef> tryDequeueBatch()
    {
        while (true)
        {
            const size_t available = count.load(std::memory_order_acq_rel);
            if (available == 0)
                return {};

            size_t localHead = head.load(std::memory_order_relaxed);

            size_t k = 0;
            for (; k < available; ++k)
            {
                Slot& slot = slots[(head + k) % cap];
                const size_t seq = slot.seq.load(std::memory_order_acquire);
                const size_t expected = (head + k) + 1;
                if (seq != expected)
                    break;
            }

            if (k == 0)
                return {};

            if (!head.compare_exchange_weak(localHead, localHead + k, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                continue;
            }

            std::vector<LsaRecordRef> out;
            out.reserve(k);

            for (size_t i = 0; i < k; ++i)
            {
                const size_t pos = head + i;
                Slot& slot = slots[pos % cap];

                LsaRecordRef* p = slot.ptr();
                out.emplace_back(std::move(*p));
                p->~LsaRecordRef();

                slot.seq.store(pos + cap, std::memory_order_release);
            }

            count.fetch_sub(k, std::memory_order_release);
        }
    }

private:
    struct Slot
    {
        std::atomic<size_t> seq{0};
        alignas(LsaRecordRef) unsigned char storage[sizeof(LsaRecordRef)];

        LsaRecordRef* ptr() noexcept
        {
            return std::launder(reinterpret_cast<LsaRecordRef*>(storage));
        }
    };

    template <class T>
    bool enqueueImpl(T&& req)
    {
        size_t localTail = tail.load(std::memory_order_relaxed);

        while (true)
        {
            Slot& slot = slots[tail % cap];
            const size_t seq = slot.seq.load(std::memory_order_acquire);

            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail);

            if (dif == 0)
            {
                if (tail.compare_exchange_weak(localTail, localTail + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    ::new (static_cast<void*>(slot.storage))
                        LsaRecordRef(std::forward<T>(req));

                    slot.seq.store(tail + 1, std::memory_order_release);
                    count.fetch_add(1, std::memory_order_release);
                    return true;
                }
            }
            else if (dif < 0)
            {
                return false;
            }
            else
            {
                localTail = tail.load(std::memory_order_relaxed);
            }
        }
    }

private:    
    const size_t cap;
    std::vector<Slot> slots;

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) std::atomic<size_t> count{0};
};
}

#endif // OSPF_FLOOD_QUEUE_HPP
