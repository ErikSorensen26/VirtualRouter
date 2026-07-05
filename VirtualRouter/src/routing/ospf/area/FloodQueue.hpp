/**
 * @file FloodQueue.hpp
 * @brief Lock-free MPSC queue for LSAs pending area-wide flooding.
 */

#ifndef OSPF_FLOOD_QUEUE_HPP
#define OSPF_FLOOD_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "ospf/area/FloodTypes.hpp"
#include "ospf/database/LSDB.hpp"

namespace routing::ospf
{
/**
 * @brief A bounded, lock-free queue of LSAs awaiting area-wide flooding.
 *
 * Implements a classic Dmitry Vyukov MPSC ring-buffer using sequence-number
 * slots.  Producers call @ref enqueue from any thread; the single consumer
 * (@ref FloodManager) calls @ref tryDequeueBatch to drain all ready items in
 * one pass.
 *
 * The queue is non-copyable and non-movable; it must be created in-place
 * (e.g., as a member of @ref FloodManager).
 *
 * @ingroup OSPF_AREA
 */
class FloodQueue
{
public:
    /**
     * @brief Construct a FloodQueue with a fixed ring-buffer capacity.
     * @param capacity Maximum number of items the queue can hold at once.
     */
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

    /**
     * @brief Return the number of items currently in the queue.
     * @return Current item count.
     */
    size_t size() const noexcept
    {
        return count.load(std::memory_order_acquire);
    }

    /**
     * @brief Return @c true when the queue contains no items.
     */
    bool empty() const noexcept
    {
        return size() == 0;
    }

    /**
     * @brief Enqueue an LSA record (lvalue overload).
     * @param req LSA record reference to copy.
     * @param r   Flood metadata.
     * @return @c true on success, @c false if the queue is full.
     */
    bool enqueue(const LsaRecordRef& req, FloodInfo r) { return enqueueImpl(req, r); }

    /**
     * @brief Enqueue an LSA record (rvalue overload).
     * @param req LSA record reference to move.
     * @param r   Flood metadata.
     * @return @c true on success, @c false if the queue is full.
     */
    bool enqueue(LsaRecordRef&& req, FloodInfo r) { return enqueueImpl(std::move(req), r); }

    /**
     * @brief Dequeue all currently ready items in one atomic batch.
     *
     * Scans from the current head for contiguous ready slots, CAS-claims
     * them, and returns the full batch.  Returns an empty vector if
     * nothing is ready or a concurrent producer has not yet committed.
     *
     * @return Vector of (FloodInfo, LsaRecordRef) pairs; may be empty.
     */
    std::vector<std::pair<FloodInfo, LsaRecordRef>> tryDequeueBatch()
    {
        while (true)
        {
            const size_t available = count.load(std::memory_order_acquire);
            if (available == 0)
                return {};

            size_t localHead = head.load(std::memory_order_relaxed);

            size_t k = 0;
            for (; k < available; ++k)
            {
                Slot& slot = slots[(localHead + k) % cap];
                const size_t seq = slot.seq.load(std::memory_order_acquire);
                const size_t expected = (localHead + k) + 1;
                if (seq != expected)
                    break;
            }

            if (k == 0)
                return {};

            if (!head.compare_exchange_weak(localHead, localHead + k, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                continue;
            }

            std::vector<std::pair<FloodInfo, LsaRecordRef>> out;
            out.reserve(k);

            for (size_t i = 0; i < k; ++i)
            {
                const size_t pos = localHead + i;
                Slot& slot = slots[pos % cap];

                LsaRecordRef* p = slot.ptr();
                out.emplace_back(slot.info, std::move(*p));
                p->~LsaRecordRef();

                slot.seq.store(pos + cap, std::memory_order_release);
            }

            count.fetch_sub(k, std::memory_order_release);
        }
    }

private:
    /// @brief One ring-buffer slot holding an in-place constructed LsaRecordRef.
    struct Slot
    {
        std::atomic<size_t> seq{0};                          ///< Sequence counter for the Vyukov protocol.
        FloodInfo info;                                      ///< Flood metadata stored alongside the record.
        alignas(LsaRecordRef) unsigned char storage[sizeof(LsaRecordRef)]; ///< Raw storage for the LsaRecordRef.

        /** @brief Return a pointer to the placement-new'd LsaRecordRef inside this slot. */
        LsaRecordRef* ptr() noexcept
        {
            return std::launder(reinterpret_cast<LsaRecordRef*>(storage));
        }
    };

    /**
     * @brief Shared enqueue implementation for both lvalue and rvalue overloads.
     * @tparam T   Deduced as @c LsaRecordRef& or @c LsaRecordRef&&.
     * @param req  The record to forward into the slot.
     * @param r    Flood metadata.
     * @return @c true on success, @c false if the queue is full.
     */
    template <class T>
    bool enqueueImpl(T&& req, FloodInfo r)
    {
        size_t localTail = tail.load(std::memory_order_relaxed);

        while (true)
        {
            Slot& slot = slots[localTail % cap];
            const size_t seq = slot.seq.load(std::memory_order_acquire);

            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(localTail);

            if (dif == 0)
            {
                if (tail.compare_exchange_weak(localTail, localTail + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    ::new (static_cast<void*>(slot.storage))
                        LsaRecordRef(std::forward<T>(req));

                    slot.info = r;
                    slot.seq.store(localTail + 1, std::memory_order_release);
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
} // namespace routing

#endif // OSPF_FLOOD_QUEUE_HPP

