/**
 * @file RegistryReference.hpp
 * @brief Reference-counted handles to config registry slots.
 */

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryBucket.hpp"
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

    /**
     * @brief Constructs a container in the unbound, no-parent state.
     *
     * The container is neither locally set nor inherited until a reference is
     * bound via @ref RegistryDatabase::emplace or @ref RegistryDatabase::ensure.
     */
    RegistryContainer() = default;

    /**
     * @brief Constructs a child container that inherits from a parent `Reference`.
     *
     * The container starts in `INHERIT` state; `bound()` returns `true` and
     * `effective()` delegates to `parent` until a local value is set.
     *
     * @param parent  The parent scope whose reference this container inherits.
     */
    explicit RegistryContainer(const T& parent) noexcept
        : state(FieldState::INHERIT),
          base(&parent)
    {}

    /**
     * @brief Constructs a child container that inherits from a sibling `RegistryContainer`.
     *
     * Convenience constructor used when building hierarchical registry structs
     * where the parent scope is itself held in a `RegistryContainer`.
     *
     * @param parent  Sibling container whose local reference this container inherits.
     */
    explicit RegistryContainer(const RegistryContainer& parent) noexcept
        : state(FieldState::INHERIT),
          base(&parent.get())
    {}

    /**
     * @brief Returns the locally-set reference without inheritance fallback.
     *
     * @return Mutable reference to the locally stored `Reference<T>`.
     * @warning Asserts if no local reference has been set (state != SET).
     */
    T& get() noexcept
    {
        return registry;
    }

    /**
     * @brief Returns the locally-set reference without inheritance fallback (const overload).
     *
     * @return Const reference to the locally stored `Reference<T>`.
     * @warning Asserts if no local reference has been set (state != SET).
     */
    const T& get() const noexcept
    {
        return registry;
    }

private:
    template <typename...>
    friend class RegistryDatabase;

    T registry; ///< Locally-set registry value;
    FieldState state; ///< Whether a local override is in effect.
    T* base{nullptr}; ///< Pointer to the parent scope's reference, if present.
};
}

#endif // REGISTRY_REFERENCE_HPP
