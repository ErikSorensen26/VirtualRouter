/**
 * @file EventManager.hpp
 * @brief Type-safe multicast event dispatcher keyed on a uint8_t-backed enum.
 */

#ifndef EVENT_MANAGER_HPP
#define EVENT_MANAGER_HPP

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <algorithm>
#include <AtomicStack.hpp>

namespace utils
{

/// @cond INTERNAL
template <typename T, typename = void>
struct hasCountType : std::false_type {};
template <typename T>
struct hasCountType<T, std::void_t<decltype(T::COUNT)>> : std::true_type {};
/// @endcond

/**
 * @brief Multicast event dispatcher that routes typed events to registered C-style callbacks.
 * @ingroup UTILS
 *
 * EventManager maintains a list of (context-pointer, function-pointer) pairs for
 * each value of @p CbType and dispatches to all of them when run() is called.
 * Callers register a `void*` context alongside the function pointer so that
 * pure C-style callbacks can reach back into their owning object without a
 * heap allocation per registration.
 *
 * Each successful registration returns an opaque @ref Id. The same Id is passed
 * to unregister() to remove that specific callback without disturbing others
 * registered for the same event type.
 *
 * ## Architectural Role
 * A generic observer hub used wherever a subsystem needs to notify an open-ended
 * set of listeners about internal state changes (e.g. interface up/down, route
 * install/withdraw). The event type vocabulary is defined by the @p CbType enum
 * supplied by each owning subsystem.
 *
 * ## Concurrency Model
 * All mutations to the callback lists (register, unregister) are serialized under
 * @ref mtx. run() takes a snapshot of the target list under the same lock before
 * releasing it, then invokes callbacks outside the lock to prevent re-entrant
 * deadlocks. Callbacks must therefore tolerate being called without the
 * EventManager lock held.
 *
 * @tparam CbType  Enum that enumerates all event types managed by this instance.
 *                 Must satisfy:
 *                 - `std::is_enum_v<CbType>` is true.
 *                 - Its underlying type is `uint8_t`.
 *                 - It declares a terminal enumerator named `COUNT` whose
 *                   integer value equals the number of valid event types.
 * @tparam Args    Additional argument types forwarded by reference to every
 *                 callback when run() is called for a given event type.
 */
template <typename CbType, typename... Args>
class EventManager
{
    static_assert(std::is_enum_v<CbType>, "CbType must be an enum type.");
    static_assert(std::is_same<typename std::underlying_type<CbType>::type, uint8_t>::value, "CbType must have 'uint8_t' as its underlying type.");
    static_assert(hasCountType<CbType>::value, "CbType must define a 'COUNT' member at the end.");

public:
    /// Raw function pointer type for event callbacks.
    using Callback = void(*)(void*, Args&...);

    /**
     * @brief Opaque registration handle returned by registerCallback().
     * @ingroup UTILS
     *
     * Convertible to uint32_t for storage but otherwise opaque to callers.
     * Pass back to unregister() to remove the associated callback.
     */
    struct Id
    {
        Id(uint32_t d) : id(d) {}
        operator uint32_t() { return id; }
    private:
        friend class EventManager;
        uint32_t id;
    };

    /**
     * @brief Internal record associating a context pointer, callback, and numeric ID.
     * @ingroup UTILS
     */
    struct Ctx
    {
        void* ctx;     ///< Caller-supplied context passed as the first argument to @ref fn.
        Callback fn;   ///< Function pointer invoked by run().
        uint32_t id;   ///< Unique registration ID; used by unregister() to locate this entry.
    };

    /**
     * @brief Registers a callback to be invoked whenever @p type is fired.
     *
     * IDs are recycled from @ref unusedIds before allocating a new one, so
     * the numeric values are not monotone across the lifetime of the manager.
     *
     * @param type  Event type this callback should be associated with.
     * @param ctx   Caller context passed as the first argument on every invocation.
     * @param fn    Function pointer to invoke. Must remain valid until unregister() is called.
     * @return      Opaque Id that uniquely identifies this registration.
     */
    Id registerCallback(CbType type, void* ctx, Callback fn)
    {
        std::lock_guard<std::mutex> lock(mtx);
        uint32_t id{};
        if (!unusedIds.pop(id))
            id = nextId++;
        callbacksByType[static_cast<uint8_t>(type)].push_back({ctx, fn, id});
        idToType[id] = type;
        return Id{id};
    }

    /**
     * @brief Removes the callback identified by @p id.
     *
     * Safe to call even if the id has already been unregistered; the call is
     * silently ignored in that case.
     *
     * @param id  Handle returned by the corresponding registerCallback() call.
     */
    void unregister(Id id)
    {
        std::lock_guard<std::mutex> lock(mtx);
        unregisterImpl(id.id);
    }

    /**
     * @brief Removes multiple callbacks in a single lock acquisition.
     *
     * More efficient than calling unregister() in a loop when tearing down a
     * subsystem that holds many registrations.
     *
     * @param ids  Vector of raw IDs (as returned by converting @ref Id to uint32_t) to remove.
     */
    void unregister(std::vector<uint32_t>& ids)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& id : ids)
            unregisterImpl(id);
    }

    /**
     * @brief Fires all callbacks registered for @p type, forwarding @p args to each.
     *
     * Takes a snapshot of the callback list under the lock, then releases the
     * lock before invoking any callback. This allows callbacks to register or
     * unregister without deadlocking, but means a callback removed during
     * dispatch may still be called once more if it was already in the snapshot.
     *
     * @param type  Event type to dispatch.
     * @param args  Arguments forwarded by reference to every registered callback.
     */
    void run(CbType type, Args&... args)
    {
        std::vector<Ctx> snap;
        {
            std::lock_guard<std::mutex> lock(mtx);
            snap = callbacksByType[static_cast<uint8_t>(type)];
        }
        for (auto& c : snap)
            c.fn(c.ctx, args...);
    }

private:
    /**
     * @brief Removes one callback entry by numeric ID; caller must hold @ref mtx.
     *
     * @param id  Raw numeric ID to remove.
     */
    void unregisterImpl(uint32_t id)
    {
        auto itType = idToType.find(id);
        if (itType == idToType.end()) return;

        CbType type = itType->second;
        auto& vec = callbacksByType[static_cast<uint8_t>(type)];
        vec.erase(std::remove_if(vec.begin(), vec.end(), [id](const Ctx& c){ return c.id == id; }), vec.end());
        idToType.erase(itType);

        unusedIds.push(id);
    }

    std::mutex mtx;                                                              ///< Guards all callback list mutations.
    std::vector<Ctx> callbacksByType[static_cast<uint8_t>(CbType::COUNT)];      ///< Per-event-type callback lists; indexed by the uint8_t value of CbType.
    std::unordered_map<uint32_t, CbType> idToType;                              ///< Reverse map from registration ID to event type; used by unregister().
    uint32_t nextId = 1;                                                         ///< Next ID to allocate when unusedIds is empty.
    types::AtomicStack<uint32_t> unusedIds;                                      ///< Recycled IDs from previous unregister() calls.
};
}

#endif // EVENT_MANAGER_HPP
