/**
 * @file InterfaceManager.h
 * @ingroup INTERFACE
 */

// InterfaceManager.h

// TODO: fix race condition

#ifndef INTERFACE_MANAGER_H
#define INTERFACE_MANAGER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <EventManager.hpp>
#include "configs/InterfaceType.hpp"

namespace types { struct IPPrefix; }

namespace interface
{

class Interface;

/**
 * @enum IfEvent
 * @brief Interface lifecycle events delivered to registered callbacks.
 *
 * Deletion is treated as DOWN — the interface fires DOWN before being removed.
 */
enum class InterfaceType : uint8_t;

/**
 * @class InterfaceManager
 * @brief Centralised store and event bus for all interfaces in a VRF.
 *
 * Owns the per-VRF interface map and exposes a callback system so that
 * protocols (EIGRP, OSPF, BGP, …) can register once and be notified
 * whenever *any* interface — present or future — transitions UP or DOWN.
 *
 * ## Callback guarantees
 * - A callback registered with `on()` fires for every interface that
 *   subsequently changes to that state, including interfaces added after
 *   the registration.
 * - Callbacks are invoked outside of any InterfaceManager lock; it is
 *   safe for a callback to call `on()` / `off()` / `get()` / `snapshot()`.
 * - The order in which callbacks fire for a single event is unspecified.
 *
 * ## Thread safety
 * - `mutex`  guards `interfaces` — use a shared_lock to read, unique_lock to write.
 * - An internal `cbMutex` guards callback registrations.
 * - `notify()` copies the relevant callbacks before invoking them, so no
 *   lock is held while protocol code runs.
 */
class InterfaceManager
{
public:
    using StateEventMgr = utils::EventManager<StateChange, Interface>;
    using IPEventMgr = utils::EventManager<IPEvent, Interface, const types::IPPrefix>;

    /**
     * @brief Add an interface to the VRF.
     * @return The inserted pointer, or nullptr if the key is already present.
     */
    Interface* add(Interface* iface, InterfaceKey key);

    /**
     * @brief Look up an interface by key.
     * @return Pointer or nullptr if not found.
     */
    Interface* get(InterfaceKey key) const;

    /**
     * @brief Remove an interface from the VRF.
     *
     * Fires a DOWN event *after* the interface has been removed from the map
     * so that callbacks cannot observe it via `get()`.
     *
     * @return true if found and removed, false otherwise.
     */
    bool remove(InterfaceKey key);

    /**
     * @brief return true if empty, otherwise false.
     */
    bool empty() const noexcept;

    /**
     * @brief Return a copy of the current interface map for safe iteration.
     */
    std::unordered_map<InterfaceKey, Interface*> snapshot() const;

    /**
     * @brief Subscribe to an interface lifecycle event.
     *  
     *
     * @param event  The event to subscribe to (UP or DOWN).
     * @param ctx    Pointer to context object used in callback.
     * @param cb     Callback invoked with the affected interface and event.
     * @return       An opaque ID that can be passed to `unsubscribe()` to unsubscribe.
     */
    StateEventMgr::Id subscribe(StateChange event, void* ctx, StateEventMgr::Callback cb);

    /**
     * @brief Subscribe to an interface IP event.
     *
     * @param event  The event to subscribe to (IP add, IP del)
     * @param ctx    Pointer to context object used in callback.
     * @param cb     Callback invoked with the affected interface and event.
     * @return       An opaque ID that can be passed to `unubscribe()` to unsubscribe.
     */
    IPEventMgr::Id subscribe(IPEvent event, void* ctx, IPEventMgr::Callback cb);

    /**
     * @brief Unsubscribe a previously registered callback.
     *
     * No-op if `id` is not currently registered.
     */
    void unsubscribe(StateEventMgr::Id id);

    /**
     * @brief Unsubscribe a previously registered callback.
     *
     * No-op if `id` is not currently registered.
     */
    void unsubscribe(IPEventMgr::Id id);

    /**
     * @brief Deliver an event to all matching subscribers.
     *
     * Called by Interface when its state changes. Safe to call from any thread.
     */
    void notify(StateChange event, Interface& iface);

    /**
     * @brief Deliver an event to all matching subscribers.
     *
     * Called by Interface when its state changes. Safe to call from any thread.
     */
    void notify(IPEvent event, Interface& iface, const types::IPPrefix& addr);

private:
    friend class Interface;

    mutable std::mutex mutex; ///< Guards `interfaces`.
    std::unordered_map<interface::InterfaceKey, Interface*> interfaces; ///< All interfaces in this VRF.

    StateEventMgr stateEventMgr;
    IPEventMgr ipEventMgr;
};

} // namespace interface

#endif // INTERFACE_MANAGER_H
