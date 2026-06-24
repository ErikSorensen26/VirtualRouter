/**
 * @file RegistryReference.hpp
 * @brief Reference-counted handles to config registry slots.
 */

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryTypes.hpp"

/**
 * @brief Typed configuration registry for all protocol scopes.
 *
 * See RegistryDatabase.hpp for the full namespace description.
 */
namespace config
{
/**
 * @brief Optional slot-reference field that supports parent-inheritance.
 * @ingroup CONFIG
 *
 * A `RegistryContainer` lives as a field inside a `SubRegistry` struct.
 * It holds either:
 * - An *inherited* reference from a parent scope (state = `INHERIT`), or
 * - A locally-set reference (state = `SET`).
 *
 * The `bound()` / `effective()` / `get()` API resolves the correct reference
 * according to the current mask state, mirroring how atomic fields walk the
 * parent chain.
 *
 * ## Lifecycle & Ownership
 * `RegistryContainer` does not own the reference; it holds an `optional` copy
 * whose destructor decrements the bucket refcount.
 *
 * @tparam T  Registry struct type of the referenced scope.
 *
 * @see Reference
 * @see SubRegistry
 */
template <typename T CONFIG_INDEX_PARAM>
class RegistryContainer : public RefContainerFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    // CONSTRUCTION

    RegistryContainer() { ptr = new T(); };
    ~RegistryContainer() { if (delFn) delFn(ptr); }

    RegistryContainer(const RegistryContainer&) = delete;
    RegistryContainer& operator=(const RegistryContainer&) = delete;

    T& get() noexcept { assert(ptr); return *ptr; }

    const T& get() const noexcept { assert(ptr); return *ptr; }

    void init() {
        delFn = [](T* p) { delete p; };
        ptr = new T();
    }

private:
    T* ptr = nullptr;
    void(*delFn)(T*) = nullptr;
};

/**
 * @brief Optional slot-reference field that supports parent-inheritance and explicit unsetting.
 * @ingroup CONFIG
 *
 * An `OptionalRegistryContainer` lives as a field inside a `SubRegistry` struct.
 * It holds one of three states:
 * - An *inherited* reference from a parent scope (state = `INHERIT`),
 * - A locally-set reference (state = `SET`), or
 * - Explicitly un-configured/empty (state = `UNSET`).
 *
 * The `bound()` / `effective()` / `get()` API resolves the correct reference
 * according to the current mask state, mirroring how optional atomic fields 
 * walk the parent chain or return nullopt/throw if completely unconfigured.
 *
 * ## Lifecycle & Ownership
 * `OptionalRegistryContainer` does not own the reference; it holds an `optional` 
 * copy whose destructor decrements the bucket refcount if a local reference is active.
 *
 * @tparam T  Registry struct type of the referenced scope.
 *
 * @see Reference
 * @see RegistryContainer
 * @see SubRegistry
 */
template <typename T CONFIG_INDEX_PARAM>
class OptionalRegistryContainer : public RefContainerFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    // CONSTRUCTION

    OptionalRegistryContainer() = default;
    ~OptionalRegistryContainer() { if (delFn) delFn(owned); }

    OptionalRegistryContainer(const OptionalRegistryContainer&) = delete;
    OptionalRegistryContainer& operator=(const OptionalRegistryContainer&) = delete;

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
        if (delFn) delFn(owned);
        owned = nullptr;
    }

private:
    void assertRegistry()
    {
        if (!owned) {
            delFn = [](T* p) { delete p; };
            owned = new T();
        }
    }

    T* owned = nullptr;
    void(*delFn)(T*) = nullptr;
};
}

#endif // REGISTRY_REFERENCE_HPP
