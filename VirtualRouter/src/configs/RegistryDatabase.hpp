/**
 * @file RegistryDatabase.hpp
 * @brief Central compile-time registry that owns one Bucket per entry type.
 */

/**
 * @defgroup CONFIG Configuration Registry
 * @brief Compile-time typed config registry. Fields accessed by type tag — no runtime string lookup.
 */

#ifndef REGISTRY_DATABASE_HPP
#define REGISTRY_DATABASE_HPP

#include "RegistryReference.hpp"
#include <tuple>
#include <cassert>

/**
 * @brief Typed configuration registry for all protocol scopes.
 *
 * The `config` namespace owns the entire compile-time registry infrastructure:
 * field types (@ref AtomicField, @ref ValueField, etc.), reference-counted
 * handles (@ref Reference), and the top-level allocation table
 * (@ref RegistryDatabase). Protocol processes receive a @ref Reference at
 * construction and never touch the database directly after that.
 */
namespace config
{

/**
 * @brief Root registry that aggregates one @ref Bucket per entry type.
 * @ingroup CONFIG
 *
 * `RegistryDatabase` is the top-level allocation table. Callers never touch
 * individual `Bucket`s directly; instead they call `create()` / `emplace()` /
 * `ensure()` / `emplaceBack()`, which return reference-counted @ref Reference
 * handles. The database itself lives for the lifetime of the process and is
 * never copied.
 *
 * ## Architectural Role
 * `RegistryDatabase` is the boundary between "allocate a new config scope" and
 * "look up an existing scope". Protocols receive a @ref Reference<T> at
 * construction time and hold it for their own lifetime. The database does not
 * know about protocols; it only manages memory and reference counts.
 *
 * ## Lifecycle & Ownership
 * - Exactly one `RegistryDatabase` instance exists (owned by the CLI engine).
 * - `create()` allocates a new slot; `emplace()` is idempotent (returns the
 *   existing reference if already allocated).
 * - Slots are destroyed automatically when the last `Reference` is dropped.
 *
 * @tparam Entries  Pack of registry struct types (e.g. `EigrpRegistry`,
 *                  `OspfRegistry`, …). Each type must be default-constructible.
 *                  One `Bucket<Entry>` is instantiated per type.
 *
 * @see Reference
 * @see Bucket
 */
template <typename... Entries>
class RegistryDatabase
{
    // INTERNAL STORAGE

    /**
     * @brief Thin wrapper that associates a @ref Bucket with its entry type.
     * @ingroup CONFIG
     *
     * One `Holder` is instantiated per type in `Entries`. The `std::tuple`
     * of all holders is the sole data member of `RegistryDatabase`.
     *
     * @tparam Entry  The registry struct type whose @ref Bucket this wraps.
     */
    template <typename Entry>
    struct Holder
    {
        Bucket<Entry> bucket; ///< The allocation table for `Entry` slots.
    };

    std::tuple<Holder<Entries>...> holders; ///< Tuple of all per-type buckets.

    /**
     * @brief Retrieves the `Holder` for entry type `T`.
     *
     * @tparam T  Entry type to look up.
     * @return Reference to the matching `Holder<T>` inside `holders`.
     */
    template <typename T>
    Holder<T>& getHolder()
    {
        return std::get<Holder<T>>(holders);
    }

public:
    // CONSTRUCTION

    /**
     * @brief Constructs an empty registry with all buckets default-initialised.
     */
    RegistryDatabase() = default;

    // BUCKET ACCESS

    /**
     * @brief Access the raw bucket for a given entry type.
     *
     * Prefer the higher-level `create()` / `emplace()` helpers; this is
     * exposed for advanced callers that need direct bucket access.
     *
     * @tparam T  Entry type whose bucket to retrieve.
     */
    template <typename T>
    Bucket<T>& bucket()
    {
        return getHolder<T>().bucket;
    }

    // SCOPE CREATION

    /**
     * @brief Allocate a new config scope with an explicit user-supplied key.
     *
     * @tparam T    Entry registry struct type.
     * @param  key  Caller-chosen identifier (e.g. AS number, process ID).
     * @return A new `Reference<T>` with refcount = 1.
     *
     * @warning Calling `create()` twice with the same key asserts in debug
     * builds. Use `emplace()` if the slot may already exist.
     */
    template <typename T>
    Reference<T> create(uint64_t key)
    {
        auto& b = bucket<T>();
        auto h = b.create(key);
        return Reference<T>(b, key, h);
    }

    /**
     * @brief Allocate a new config scope parented to an existing one.
     *
     * The child `SubRegistry` will inherit field values from `parent` unless
     * explicitly overridden (masked).
     *
     * @tparam T       Entry registry struct type.
     * @param  key     Caller-chosen identifier.
     * @param  parent  Reference to the parent scope.
     * @return A new `Reference<T>` with refcount = 1.
     */
    template <typename T>
    Reference<T> create(uint64_t key, Reference<T>& parent)
    {
        auto& b = bucket<T>();
        auto h = b.create(key, parent.get());
        return Reference<T>(b, key, h);
    }

    /**
     * @brief Allocate a new config scope with an auto-generated key.
     *
     * Useful when the caller does not have a meaningful external identifier.
     * The auto-key is a monotonically increasing counter local to the bucket.
     *
     * @tparam T  Entry registry struct type.
     * @return A new `Reference<T>` with refcount = 1.
     */
    template <typename T>
    Reference<T> create()
    {
        auto& b = bucket<T>();
        auto h = b.createAuto();
        return Reference<T>(b, b.slotKey(h), h);
    }

    /**
     * @brief Allocate a new parented config scope with an auto-generated key.
     *
     * @tparam T       Entry registry struct type.
     * @param  parent  Reference to the parent scope.
     * @return A new `Reference<T>` with refcount = 1.
     */
    template <typename T>
    Reference<T> create(Reference<T>& parent)
    {
        auto& b = bucket<T>();
        auto h = b.createAuto(parent.get());
        return Reference<T>(b, b.slotKey(h), h);
    }

    // IDEMPOTENT AND FORCED ALLOCATION

    /**
     * @brief Idempotent allocation: returns the existing reference if already
     *        bound, otherwise allocates a new auto-keyed scope.
     *
     * @tparam T          Entry registry struct type.
     * @param  container  The `ReferenceContainer` field on a registry struct
     *                    that will hold the result.
     * @return The existing or newly created `Reference<T>`.
     */
    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> emplace(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container)
    {
        if (container.bound())
            return container.ref.value();

        Reference<T> ref = container.base
            ? create<T>(*container.base)
            : create<T>();
        container.setLocal(ref);
        return ref;
    }

    /**
     * @brief Idempotent allocation with an explicit parent scope.
     *
     * If the container already holds a reference it is returned unchanged.
     * Otherwise a new scope is allocated as a child of `parent`.
     *
     * @tparam T          Entry registry struct type.
     * @param  container  Target `ReferenceContainer` field.
     * @param  parent     Parent scope reference.
     * @return The existing or newly created `Reference<T>`.
     */
    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> emplace(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, Reference<T>& parent)
    {
        if (container.ref.has_value())
            return container.ref.value();

        if (!container.base)
            container.base = &parent;

        Reference<T> ref(create<T>(*container.base));
        container.setLocal(ref);
        return ref;
    }

    /**
     * @brief Force-allocate a fresh scope, replacing any previously bound one.
     *
     * Unlike `emplace()`, `ensure()` always creates a new allocation and
     * replaces the existing binding in `container`. Used when a config scope
     * must be recreated from scratch (e.g. on `no router eigrp` followed by
     * `router eigrp`).
     *
     * @tparam T          Entry registry struct type.
     * @param  container  Target `ReferenceContainer` field.
     * @return The newly created `Reference<T>`.
     */
    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> ensure(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container)
    {
        Reference<T> ref = container.base
            ? create<T>(*container.base)
            : create<T>();
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }

    /**
     * @brief Force-allocate a fresh scope under a specific parent, replacing
     *        any previously bound one.
     *
     * @tparam T          Entry registry struct type.
     * @param  container  Target `ReferenceContainer` field.
     * @param  parent     Parent scope reference.
     * @return The newly created `Reference<T>`.
     */
    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> ensure(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, Reference<T>& parent)
    {
        container.base = &parent;
        Reference<T> ref = create<T>(*container.base);
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }

    // LIST FIELD OPERATIONS

    /**
     * @brief Insert a new entry into an `OwnedListField` keyed by `id`, or
     *        return the existing entry if `id` is already present.
     *
     * @tparam T     Entry registry struct type.
     * @tparam K     Key type used to identify list entries (e.g. `uint32_t`
     *               for AS number or area ID).
     * @param  list  The `OwnedListField` to insert into.
     * @param  id    Key for the new entry.
     * @return A `Reference<T>` for the newly created or pre-existing entry.
     */
    template <typename T, typename K CONFIG_INDEX_PARAM>
    Reference<T> emplaceBack(OwnedListField<T, K CONFIG_INDEX_ARG(F)>& list, const K& id)
    {
        if (auto it = list.children.find(id); it != list.children.end())
            return it->second;
        return list.getMutable().emplace(id, create<T>()).first->second;
    }

    /**
     * @brief Insert a new parented entry into an `OwnedListField`, or return
     *        the existing entry if `id` is already present.
     *
     * @tparam T       Entry registry struct type.
     * @tparam K       Key type.
     * @param  list    The `OwnedListField` to insert into.
     * @param  id      Key for the new entry.
     * @param  parent  Parent scope whose values the new entry inherits.
     * @return A `Reference<T>` for the newly created or pre-existing entry.
     */
    template <typename T, typename K CONFIG_INDEX_PARAM>
    Reference<T> emplaceBack(OwnedListField<T, K CONFIG_INDEX_ARG(F)>& list, const K& id, const Reference<T>& parent)
    {
        if (auto it = list.children.find(id); it != list.children.end())
            return it->second;
        return list.getMutable().emplace(id, create<T>(const_cast<Reference<T>&>(parent))).first->second;
    }
};
}

#endif // REGISTRY_HPP
