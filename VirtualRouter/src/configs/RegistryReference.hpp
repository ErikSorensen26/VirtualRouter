/**
 * @file RegistryReference.hpp
 * @brief Reference-counted handles to config registry slots.
 */

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include <optional>
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
 * @brief A reference-counted, non-owning handle to a `Bucket<T>` slot.
 * @ingroup CONFIG
 *
 * `Reference<T>` is the primary way callers hold a config registry scope.
 * Each copy increments the bucket's reference count; the destructor decrements
 * it. When the count reaches zero the slot is destroyed and the memory is
 * returned to the free list.
 *
 * ## Lifecycle & Ownership
 * - Created exclusively by @ref RegistryDatabase factory methods.
 * - Copy-constructible and move-constructible; both forms addRef on the slot.
 * - Assignment is deleted to prevent accidental rebinding.
 * - Protocols typically store a `Reference` by value as a member.
 *
 * ## Concurrency Model
 * The copy constructor / destructor call `Bucket::addRef` / `Bucket::releaseRef`
 * which are not thread-safe. References must not be copied or destroyed
 * concurrently on the same slot without external synchronisation.
 *
 * @tparam T  Registry struct type (e.g. `EigrpRegistry`, `OspfRegistry`).
 *
 * @see RegistryDatabase
 * @see Bucket
 */
template <typename T>
class Reference
{
public:
    // CONSTRUCTION AND DESTRUCTION

    /**
     * @brief Copy-constructs a new handle, incrementing the slot's reference count.
     *
     * @param other  Existing handle to copy.
     */
    Reference(const Reference& other) noexcept
        : key(other.key),
          bucket(other.bucket),
          handle(other.handle),
          ref(other.ref)
    {
        bucket.addRef(handle);
    }

    /**
     * @brief Move-constructs a new handle.
     *
     * The moved-from handle remains valid (the bucket refcount is incremented
     * for the new handle), so both the source and the destination are usable
     * after the move.
     *
     * @param other  Handle to move from.
     */
    Reference(Reference&& other) noexcept
        : key(other.key),
          bucket(other.bucket),
          handle(other.handle),
          ref(other.ref)
    {
        bucket.addRef(handle);
    }

    /// @brief Assignment is deleted to prevent accidental slot rebinding.
    Reference& operator=(const Reference&) = delete;
    /// @brief Move-assignment is deleted to prevent accidental slot rebinding.
    Reference& operator=(Reference&&) = delete;

    /**
     * @brief Decrements the slot's reference count; destroys the slot when it reaches zero.
     */
    ~Reference()
    {
        bucket.releaseRef(handle);
    }

    // ACCESSORS

    /**
     * @brief Returns the caller-assigned key used to identify this slot.
     *
     * @return The `uint64_t` key supplied to `RegistryDatabase::create()`.
     */
    uint64_t getKey() const noexcept
    {
        return key;
    }

    /**
     * @brief Provides pointer-style access to the underlying registry struct.
     * @return Pointer to the `T` stored in this slot.
     */
    T* operator->() noexcept
    {
        return &ref;
    }

    /**
     * @brief Provides const pointer-style access to the underlying registry struct.
     * @return Const pointer to the `T` stored in this slot.
     */
    const T* operator->() const noexcept
    {
        return &ref;
    }

    /**
     * @brief Returns a mutable reference to the underlying registry struct.
     * @return Reference to the `T` stored in this slot.
     */
    T& get() noexcept
    {
        return ref;
    }

    /**
     * @brief Returns a const reference to the underlying registry struct.
     * @return Const reference to the `T` stored in this slot.
     */
    const T& get() const noexcept
    {
        return ref;
    }

private:
    // PRIVATE CONSTRUCTION (called only by RegistryDatabase)

    /**
     * @brief Constructs a handle from raw bucket components.
     *
     * Only @ref RegistryDatabase is permitted to call this constructor.
     *
     * @param b  Bucket that owns the slot.
     * @param k  Caller key for this slot.
     * @param h  Opaque handle identifying the slot within the bucket.
     */
    Reference(Bucket<T>& b, uint64_t k, const typename Bucket<T>::Handle& h)
        : key(k),
          bucket(b),
          handle(h),
          ref(b.get(h))
    {
        bucket.addRef(handle);
    }

    // PRIVATE MEMBERS

    uint64_t key{};                       ///< Caller-assigned slot identifier.
    Bucket<T>& bucket;                    ///< Owning bucket; used for addRef/releaseRef.
    typename Bucket<T>::Handle handle{};  ///< Opaque index into the bucket's slot array.
    T& ref;                               ///< Direct reference to the live `T` object.

    template <typename...>
    friend class RegistryDatabase;
};

/**
 * @brief Optional slot-reference field that supports parent-inheritance.
 * @ingroup CONFIG
 *
 * A `ReferenceContainer` lives as a field inside a `SubRegistry` struct.
 * It holds either:
 * - An *inherited* reference from a parent scope (state = `INHERIT`), or
 * - A locally-set reference (state = `SET`).
 *
 * The `bound()` / `effective()` / `get()` API resolves the correct reference
 * according to the current mask state, mirroring how atomic fields walk the
 * parent chain.
 *
 * ## Lifecycle & Ownership
 * `ReferenceContainer` does not own the reference; it holds an `optional` copy
 * whose destructor decrements the bucket refcount.
 *
 * @tparam T  Registry struct type of the referenced scope.
 *
 * @see Reference
 * @see SubRegistry
 */
template <typename T CONFIG_INDEX_PARAM>
class ReferenceContainer : public RefContainerFieldFlag
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
    ReferenceContainer() = default;

    /**
     * @brief Constructs a child container that inherits from a parent `Reference`.
     *
     * The container starts in `INHERIT` state; `bound()` returns `true` and
     * `effective()` delegates to `parent` until a local value is set.
     *
     * @param parent  The parent scope whose reference this container inherits.
     */
    explicit ReferenceContainer(const Reference<T>& parent) noexcept
        : ref(std::nullopt),
          state(MaskState::INHERIT),
          base(&parent)
    {}

    /**
     * @brief Constructs a child container that inherits from a sibling `ReferenceContainer`.
     *
     * Convenience constructor used when building hierarchical registry structs
     * where the parent scope is itself held in a `ReferenceContainer`.
     *
     * @param parent  Sibling container whose local reference this container inherits.
     */
    explicit ReferenceContainer(const ReferenceContainer& parent) noexcept
        : ref(std::nullopt),
          state(MaskState::INHERIT),
          base(&parent.local())
    {}

    /**
     * @brief Returns true if a reference is available (locally set or inherited).
     */
    bool bound() const noexcept
    {
        if (state == MaskState::SET)
            return ref.has_value();

        return base != nullptr;
    }

    /**
     * @brief Returns the effective reference — the local one if SET, otherwise
     *        the inherited one from the parent scope.
     *
     * @warning Asserts if called when neither a local ref nor a base pointer
     *          is present. Check `bound()` first.
     */
    const Reference<T>& effective() const noexcept
    {
        if (base && state == MaskState::INHERIT)
            return *base;

        bool buh = ref.has_value();
        assert(buh);
        return *ref;
    }

    /**
     * @brief Returns the effective (resolved) reference.
     *
     * Equivalent to `effective()`; asserts that `bound()` is true.
     *
     * @return The locally-set reference, or the inherited parent reference.
     * @warning Undefined behaviour (assertion) if called when not bound.
     */
    const Reference<T>& get() const noexcept
    {
        assert(bound());
        return effective();
    }

    /**
     * @brief Returns the locally-set reference without inheritance fallback.
     *
     * @return Mutable reference to the locally stored `Reference<T>`.
     * @warning Asserts if no local reference has been set (state != SET).
     */
    Reference<T>& local() noexcept
    {
        assert(ref.has_value());
        return *ref;
    }

    /**
     * @brief Returns the locally-set reference without inheritance fallback (const overload).
     *
     * @return Const reference to the locally stored `Reference<T>`.
     * @warning Asserts if no local reference has been set (state != SET).
     */
    const Reference<T>& local() const noexcept
    {
        assert(ref.has_value());
        return *ref;
    }
    /**
     * @brief Clears the local reference and reverts state to INHERIT.
     */
    void unset() noexcept
    {

    }

private:
    template <typename...>
    friend class RegistryDatabase;

    /**
     * @brief Binds a local reference and transitions state to SET.
     *
     * @param r  Reference to store as the local value.
     */
    void setLocal(const Reference<T>& r) noexcept
    {
        ref.emplace(r);
        state = MaskState::SET;
    }

    /**
     * @brief Clears the local reference and reverts state to INHERIT.
     */
    void unsetLocal() noexcept
    {
        ref.reset();
        state = MaskState::INHERIT;
    }

    // PRIVATE MEMBERS

    std::optional<Reference<T>> ref{std::nullopt}; ///< Locally-set reference, if any.
    MaskState state{MaskState::INHERIT};            ///< Whether a local override is in effect.
    Reference<T>* base{nullptr};                   ///< Pointer to the parent scope's reference, if present.
};
}

#endif // REGISTRY_REFERENCE_HPP
