/**
 * @file FloodManager.h
 * @brief LSA flooding coordinator for a single OSPF area.
 */

#ifndef FLOOD_MANAGER_H
#define FLOOD_MANAGER_H

#include <cstdint>
#include <atomic>
#include "FloodQueue.hpp"

namespace core { class ProcessQueue; }

namespace routing::ospf
{
class Area;
class OspfRib;

/**
 * @brief Coordinates LSA flooding across all interfaces in one OSPF area.
 *
 * Incoming and self-originated LSAs are enqueued via @ref enqueueFlood.
 * A coalescing timer fires shortly after the first enqueue and calls
 * @ref runFlood, which drains the @ref FloodQueue and dispatches each LSA
 * to every eligible interface, respecting the RFC 2328 §13.3 flood rules
 * (back-flood suppression, retransmission list updates, etc.).
 *
 * @ingroup OSPF_AREA
 */
class FloodManager
{
public:
    /**
     * @brief Construct a FloodManager for the given area.
     * @param area The area whose interfaces this manager floods on.
     */
    explicit FloodManager(Area& area);

    /**
     * @brief Enqueue an LSA for flooding (lvalue overload).
     * @param record Reference-counted handle to the LSA record.
     * @param info   Flood metadata (reason, etc.).
     */
    void enqueueFlood(LsaRecordRef& record, const FloodInfo& info);

    /**
     * @brief Enqueue an LSA for flooding (rvalue overload).
     * @param record Reference-counted handle to the LSA record (moved).
     * @param info   Flood metadata (reason, etc.).
     */
    void enqueueFlood(LsaRecordRef&& record, const FloodInfo& info);

    /**
     * @brief Cancel the coalescing flood timer, if armed.
     *
     * Called from ~Area() so no flood-timer callback can fire (and touch
     * this area's LSDB/interfaces) after the area starts tearing down.
     */
    void cancel();

private:
    /** @brief Arm the coalescing flood timer if not already active. */
    void startFloodTimer();

    /** @brief Timer callback — calls runFlood() on the process queue. */
    void onFloodTimer();

    /** @brief Drain the flood queue and transmit pending LSAs. */
    void runFlood();

private:
    Area& area;             ///< Owning area whose interfaces are flooded.
    FloodQueue fq;          ///< Lock-free queue of pending flood items.

    std::atomic<bool> timerActive{0}; ///< Guards against duplicate timer arms.
    uint32_t timerId;                 ///< Handle of the active coalescing timer.
};
} // namespace routing

#endif // FLOOD_MANAGER_H

