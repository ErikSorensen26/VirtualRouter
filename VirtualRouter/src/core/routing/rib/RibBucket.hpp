/**
 * @file RibBucket.hpp
 * @brief Per-prefix RIB bucket holding all candidate routes for one prefix.
 * @ingroup CORE_ROUTING_RIB
 */

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

/**
 * @brief Container for all RIB routes associated with a single network prefix.
 *
 * One `RibBucket` exists per unique (prefix, length) key in the RIB.  It
 * stores the full set of candidate routes contributed by all routing protocols
 * and process instances, selects the single best route, and maintains an
 * RCU-safe heap copy in `fibEntry` for lock-free data-plane reads.
 *
 * ## Architectural Role
 * `RibBucket` objects are heap-allocated and owned by `Rib<AddrType>`.  The
 * `Fib` holds a raw pointer to each bucket's `fibEntry` atomic; swapping that
 * atomic is the only FIB update mechanism.
 *
 * ## Lifecycle & Ownership
 * - Created by `Rib::installRoute()` on first route installation for a prefix.
 * - Destroyed (via `RCU::retire`) by `Rib::withdrawRoute()` when the last
 *   route for the prefix is removed.
 * - The destructor retires the outstanding `fibEntry` heap copy.
 *
 * ## Concurrency Model
 * - All mutations (`addRoute`, `removeRoute`, `selectBest`, `clear`) are
 *   called exclusively from the RIB's `ProcessQueue` scheduler thread.
 * - `fibEntry` is read lock-free by the data plane under an `RCU::Guard`.
 * - `empty()` checks the atomic `fibEntry` and is safe from any thread.
 *
 * @tparam AddrType Unsigned integral address type (`uint32_t` / `__uint128_t`).
 *
 * @see Rib
 * @see RibEntry
 * @see RouteWatcher
 */
template <typename AddrType>
class RibBucket
{
    static void deleter(void* val) { delete reinterpret_cast<RibEntry<AddrType>*>(val); }
public:
    std::vector<RibEntry<AddrType>> routes;  ///< All candidate routes for this prefix.

    RibEntry<AddrType>* bestEntry = nullptr; ///< Pointer into `routes` for the current best route.
    RibEntry<AddrType>* prevBest  = nullptr; ///< Previous best route before the last `selectBest()` call; used by `RouteWatcher`.

    std::atomic<RibEntry<AddrType>*> fibEntry{nullptr}; ///< RCU-protected heap copy of the best entry, read by the FIB.

    RibBucket() noexcept = default;

    /**
     * @brief Destroy the bucket, retiring any outstanding FIB heap copy via RCU.
     */
    ~RibBucket()
    {
        RibEntry<AddrType>* val = fibEntry.exchange(nullptr, std::memory_order_acq_rel);
        if (val) utils::RCU::retire(deleter, val);
    }

    // ROUTE MANAGEMENT

    /**
     * @brief Insert or update a route entry in the bucket.
     *
     * If an entry with the same (source, processId) already exists its fields
     * are updated in-place; if nothing changed the bucket is left unmodified
     * and `false` is returned.  Otherwise the entry is appended.  In all
     * cases `selectBest()` is run afterward and the FIB is updated.
     *
     * @warning Takes ownership of `e`; callers must not access `e` after this
     *          call.  The bucket calls `delete e` whether or not the entry is
     *          kept.
     *
     * @param e Heap-allocated route entry to install.
     * @return `true` if the bucket's best route changed and the FIB was
     *         updated; `false` if the entry was a no-op duplicate.
     */
    bool addRoute(const RibEntry<AddrType>* e) noexcept
    {
        bool replaced = false;

        for (RibEntry<AddrType>& r : routes)
        {
            if (r.source == e->source && r.processId == e->processId)
            {
                bool sameNextHops = r.nextHopCount == e->nextHopCount;
                for (uint8_t i = 0; sameNextHops && i < e->nextHopCount; ++i)
                    sameNextHops = r.nextHops[i].nextHop == e->nextHops[i].nextHop &&
                                   r.nextHops[i].iface   == e->nextHops[i].iface;

                if (r.metric        == e->metric        &&
                    r.adminDistance == e->adminDistance &&
                    sameNextHops)
                {
                    delete e;
                    return false;
                }
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

    /**
     * @brief Remove all routes from the given (source, processId) pair.
     *
     * If the removal changes the best route, `selectBest()` updates the FIB.
     *
     * @param src Protocol source to remove.
     * @param pid Process instance ID (0 = any).
     */
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

    // BEST-ROUTE QUERIES

    /**
     * @brief Return the best route contributed by a specific (source, processId).
     *
     * Selection criterion: lowest admin distance, then lowest metric.
     *
     * @param src Protocol source filter.
     * @param pid Process instance ID filter.
     * @return Pointer into `routes`, or `nullptr` if no matching entry.
     */
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

    /**
     * @brief Return the best route for any source from a specific process ID.
     * @param pid Process instance ID filter.
     * @return Pointer into `routes`, or `nullptr`.
     */
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

    /**
     * @brief Return the best route from a specific source type across all processes.
     * @param src Protocol source filter.
     * @return Pointer into `routes`, or `nullptr`.
     */
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

    /**
     * @brief Return the overall best route across all sources and processes.
     * @return `bestEntry`, which is updated by every call to `selectBest()`.
     */
    RibEntry<AddrType>* getBestRoute() noexcept
    {
        return bestEntry;
    }

    // INTERNAL

    /**
     * @brief Recompute the best route and atomically publish a heap copy to the FIB.
     *
     * Sets `prevBest` to the old `bestEntry`, then scans `routes` for the
     * entry with the lowest (adminDistance, metric).  A fresh heap copy is
     * swapped into `fibEntry`; the displaced pointer is retired via
     * `RCU::retire` so in-flight data-plane readers finish safely.
     */
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
        if (old) utils::RCU::retire(deleter, old);
    }

    /**
     * @brief Return `true` if no route is currently published to the FIB.
     */
    bool empty() const noexcept
    {
        return fibEntry.load(std::memory_order_acquire) == nullptr;
    }

    /**
     * @brief Remove all routes and retire the current FIB entry.
     */
    void clear() noexcept
    {
        RibEntry<AddrType>* old = fibEntry.exchange(nullptr, std::memory_order_acq_rel);
        if (old) utils::RCU::retire(deleter, old);
        routes.clear();
        bestEntry = nullptr;
        prevBest  = nullptr;
    }
};

} // namespace core

#endif // RIB_BUCKET_HPP
