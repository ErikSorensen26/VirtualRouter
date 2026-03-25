// InterfaceManager.h

// TODO: fix race condition

#ifndef INTERFACE_MANAGER_H
#define INTERFACE_MANAGER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <EventManager.hpp>

namespace types { struct IPv4Prefix; struct IPv6Prefix; }

namespace interface
{

class Interface;

/**
 * @enum IfEvent
 * @brief Interface lifecycle events delivered to registered callbacks.
 *
 * Deletion is treated as DOWN — the interface fires DOWN before being removed.
 */
enum class InterfaceType : uint8_t; ///< Forward declaration of InterfaceType.

/**
 * @enum StateChange
 * @brief Represents interface up/down state changes.
 */
enum class StateChange : uint8_t
{
    /* --- System --- */
    IF_READY,
    IF_DOWN,
    COUNT
};

/**
 * @enum IPv4Event
 * @brief Represents interface ipv4 events
 */
enum class IPv4Event : uint8_t
{
    /* --- IPv4 (simple) --- */
    IPV4_READY,
    IPV4_DEL,
    IPV4_SECONDARY_READY,
    IPV4_SECONDARY_DEL,
    IPV4_CONFLICT,
    COUNT
};

/**
 * @enum IPv4Event
 * @brief Represents interface ipv4 events
 */
enum class IPv6Event : uint8_t
{
    /* --- IPv6 (specific) --- */
    IPV6_LL_READY,
    IPV6_LL_DEL,
    IPV6_LL_CONFLICT,
    IPV6_READY,
    IPV6_DEL,
    IPV6_CONFLICT,
    COUNT
};

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
    using IPv4EventMgr = utils::EventManager<IPv4Event, Interface, types::IPv4Prefix>;
    using IPv6EventMgr = utils::EventManager<IPv6Event, Interface, types::IPv6Prefix>;


    /**
     * @brief Add an interface to the VRF.
     * @return The inserted pointer, or nullptr if the key is already present.
     */
    Interface* add(Interface* iface, uint32_t key);

    /**
     * @brief Look up an interface by key.
     * @return Pointer or nullptr if not found.
     */
    Interface* get(uint32_t key) const;

    /**
     * @brief Remove an interface from the VRF.
     *
     * Fires a DOWN event *after* the interface has been removed from the map
     * so that callbacks cannot observe it via `get()`.
     *
     * @return true if found and removed, false otherwise.
     */
    bool remove(uint32_t key);

    /**
     * @brief return true if empty, otherwise false.
     */
    bool empty() const noexcept;

    /**
     * @brief Return a copy of the current interface map for safe iteration.
     */
    std::unordered_map<uint32_t, Interface*> snapshot() const;

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
     * @brief Subscribe to an interface IPv4 event.
     *
     * @param event  The event to subscribe to (IP add, IP del)
     * @param ctx    Pointer to context object used in callback.
     * @param cb     Callback invoked with the affected interface and event.
     * @return       An opaque ID that can be passed to `unubscribe()` to unsubscribe.
     */
    IPv4EventMgr::Id subscribe(IPv4Event event, void* ctx, IPv4EventMgr::Callback cb);

    /**
     * @brief Subscribe to an interface IPv6 event.
     *
     * @param event  The event to subscribe to (IP add, IP del)
     * @param ctx    Pointer to context object used in callback.
     * @param cb     Callback invoked with the affected interface and event.
     * @return       An opaque ID that can be passed to `unsubscribe()` to unsubscribe.
     */
    IPv6EventMgr::Id subscribe(IPv6Event event, void* ctx, IPv6EventMgr::Callback cb);

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
    void unsubscribe(IPv4EventMgr::Id id);

    /**
     * @brief Unsubscribe a previously registered callback.
     *
     * No-op if `id` is not currently registered.
     */
    void unsubscribe(IPv6EventMgr::Id id);

private:
    friend class Interface;

    mutable std::mutex mutex; ///< Guards `interfaces`.
    std::unordered_map<uint32_t, Interface*> interfaces; ///< All interfaces in this VRF.


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
    void notify(IPv4Event event, Interface& iface, types::IPv4Prefix addr);

    /**
     * @brief Deliver an event to all matching subscribers.
     *
     * Called by Interface when its state changes. Safe to call from any thread.
     */
    void notify(IPv6Event event, Interface& iface, types::IPv6Prefix addr);

    StateEventMgr stateEventMgr;
    IPv4EventMgr ipv4EventMgr;
    IPv6EventMgr ipv6EventMgr;
};

} // namespace interface

#endif // INTERFACE_MANAGER_H
