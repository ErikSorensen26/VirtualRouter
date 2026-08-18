/**
 * @file RegistryReference.hpp
 * @brief Reference-counted handles to config registry slots.
 */

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryTypes.hpp"

namespace config
{
/**
 * @brief Always-present slot-reference field inside a `SubRegistry` struct.
 * @ingroup CONFIG
 *
 * A `RegistryContainer` owns a heap-allocated `T`, unconditionally allocated
 * by the constructor so `get()` is always valid. `init()` replaces it with a
 * fresh `T`, discarding whatever was there before.
 *
 * @tparam T  Registry struct type of the referenced scope.
 *
 * @see OptionalRegistryContainer
 * @see SubRegistry
 */
template <typename T, auto H = nullptr, auto V = nullptr>
class RegistryContainer : public CallbackHolder<RefContainerFieldFlag, T, H, V>
{
public:
    using type = T;

    // CONSTRUCTION

    template <typename S, auto EF>
    RegistryContainer(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : CallbackHolder<RefContainerFieldFlag, T, H, V>(t, i),
          configIndex(static_cast<uint32_t>(EF))
    {
        ptr = new T();
    }
    ~RegistryContainer() { if (ptr) delete ptr; }

    RegistryContainer(const RegistryContainer&) = delete;
    RegistryContainer& operator=(const RegistryContainer&) = delete;

    T& get() noexcept { assert(ptr); return *ptr; }

    const T& get() const noexcept { assert(ptr); return *ptr; }

    void init() {
        delete ptr; // the ctor already allocated; don't orphan it
        ptr = new T();
    }

private:
    T* ptr = nullptr;
    uint32_t configIndex;
};

/**
 * @brief Lazily-allocated, resettable slot-reference field inside a `SubRegistry` struct.
 * @ingroup CONFIG
 *
 * An `OptionalRegistryContainer` owns a heap-allocated `T`, allocated on first
 * use (`get()`/`assertRegistry()`) rather than at construction, and freeable
 * again via `reset()`; `hasValue()` reports whether it is currently allocated.
 *
 * @tparam T  Registry struct type of the referenced scope.
 * @tparam H  Optional applier callback, invoked like other `CallbackHolder` fields.
 * @tparam V  Optional validator callback, invoked like other `CallbackHolder` fields.
 *
 * @see RegistryContainer
 * @see SubRegistry
 */
template <typename T, auto H = nullptr, auto V = nullptr>
class OptionalRegistryContainer : public OptionalCallbackHolder<OptionalRefContainerFieldFlag, T, H, V>
{
public:
    using type = T;

    // CONSTRUCTION

    template <typename S, auto EF>
    OptionalRegistryContainer(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : OptionalCallbackHolder<OptionalRefContainerFieldFlag, T, H, V>(t, i),
          configIndex(static_cast<uint32_t>(EF))
    {}
    ~OptionalRegistryContainer() { reset(); }

    OptionalRegistryContainer(const OptionalRegistryContainer&) = delete;
    OptionalRegistryContainer& operator=(const OptionalRegistryContainer&) = delete;

    void setOwner(utils::Any owner) noexcept
    {
        ownerBack = owner;
    }

    T& get() noexcept
    {
        assertRegistry();
        return *owned;
    }

    const T& get() const noexcept
    {
        assertRegistry();
        return *owned;
    }

    bool hasValue() const noexcept
    {
        return owned != nullptr;
    }

    void reset() noexcept
    {
        if (owned) delete owned;
        owned = nullptr;
    }

private:
    void assertRegistry()
    {
        if (!owned)
        {
            owned = new T();
            if (ownerBack.hasValue())
                owned->setParent(ownerBack);
        }
    }

    T* owned = nullptr;
    utils::Any ownerBack;
    uint32_t configIndex;
};
}

#endif // REGISTRY_REFERENCE_HPP
