/**
 * @file RegistryTypes.hpp
 * @brief Core field types, flag traits, and C++ concepts used by the registry.
 */


#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>
#include <utility>
#include <unordered_map>
#include <vector>

#define ENABLE_CONFIG_INDEX 0

#if defined(NDEBUG)
    #define USE_CONFIG_INDEX 0
#else
    #if ENABLE_CONFIG_INDEX
        #define USE_CONFIG_INDEX 1
    #else
        #define USE_CONFIG_INDEX 0
    #endif
#endif

#if USE_CONFIG_INDEX
    #define CONFIG_INDEX_PARAM , auto F
    #define CONFIG_INDEX_ARG(x) , x
    #define CONFIG_INDEX_MEMBER static constexpr auto field = F;
#else
    #define CONFIG_INDEX_PARAM
    #define CONFIG_INDEX_ARG(x)
    #define CONFIG_INDEX_MEMBER
#endif

#define IGNOR(type) config::IgnoreCompare<type>

/**
 * @brief Typed configuration registry for all protocol scopes.
 *
 * See RegistryDatabase.hpp for the full namespace description.
 */
namespace config
{
// FORWARD DECLARATIONS

template <typename...>
class RegistryDatabase;

template <typename ENUM, typename... Fields>
class SubRegistry;

// TYPE ALIASES

using ApplyFn  = void (*)(void* ctx); ///< Callback signature for live-notification appliers.

/**
 * @brief Satisfied by callables that accept a `T&` and return `bool` indicating whether the value changed.
 *
 * Used as the constraint on the lambda passed to `ListField::withWrite`.
 * Returning `true` signals that the applier should be fired after the lock is released.
 *
 * @tparam T  The guarded value type.
 * @tparam F  Callable type to test.
 */
template <typename T, typename F>
concept WriteFn = requires(F f, T t)
{
    { f(t) } -> std::same_as<bool>;
};

// FIELD FLAG TAGS

/**
 * @brief Base flag type that marks a field as an atomic (lock-free) config field.
 *
 * Types derived from `AtomicFieldFlag` satisfy the @ref IsAtomicField concept
 * (unless they also derive from `OptionalAtomicFieldFlag`).
 */
struct AtomicFieldFlag {};

/**
 * @brief Flag type that marks a field as an optional (nullable) atomic config field.
 * @ingroup CONFIG
 *
 * Derives from `AtomicFieldFlag` so that a single `std::derived_from<T, AtomicFieldFlag>`
 * check catches both required and optional atomic fields; the @ref IsOptionalAtomicField
 * concept further discriminates between them.
 */
struct OptionalAtomicFieldFlag : AtomicFieldFlag {};

/**
 * @brief Flag type that marks a field as a reference-container (sub-scope pointer).
 * @ingroup CONFIG
 *
 * Used by @ref RegistryContainer to satisfy the @ref IsRefContainer concept.
 */
struct RefContainerFieldFlag {};

/**
 * @brief Flag type that marks a field as a mutex-guarded value config field.
 * @ingroup CONFIG
 *
 * Types derived from `ListFieldFlag` satisfy the @ref IsListField concept.
 */
struct ListFieldFlag {};

/**
 * @brief Flag type that marks a field as an optional mutex-guarded value config field.
 * @ingroup CONFIG
 *
 * Types derived from `ValueFieldFlag` satisfy the @ref IsValueField concept.
 */
struct ValueFieldFlag {};

/**
 * @brief Flag type that marks a field as an owned map of child registry entries.
 * @ingroup CONFIG
 *
 * Types derived from `OwnedListFieldFlag` satisfy the @ref IsOwnedListField concept.
 */
struct OwnedListFieldFlag {};

/**
 * @brief Flag type that marks a tuple field as a compare type.
 * @ingroup CONFIG
 *
 * Types derived by 'IgnoreCompareFlag' satisfy the @ref IsIgnoreCompare concept.
 */
struct IgnoreCompareFlag {};

// CONCEPTS

/**
 * @brief Satisfied by any registry field type that exposes a nested `::type` alias.
 *
 * Every field class in the registry (`AtomicField`, `ListField`, etc.) must
 * declare `using type = T;` where `T` is the value type it stores. This concept
 * is the root constraint used by all other field concepts.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsFieldBase =
    requires
    {
        typename T::type;
    };

/**
 * @brief Indicates whether a field uses its own value or inherits from its parent.
 * @ingroup CONFIG
 */
enum class FieldState : uint8_t
{
    INHERIT, ///< Field defers to its parent scope; no local override.
    CANNED,  ///< Field has a locally-set value that shadows the parent.
    UNSET,   ///< Field is not currently set, but does not support inheritance.
};

/**
 * @brief Holds a type-erased context pointer used by applier callbacks.
 * @ingroup CONFIG
 *
 * When a protocol process registers itself with a registry field (via
 * `SubRegistry::context().set(ptr)`), the `ContextProvider` stores the raw
 * pointer. Fields with an `ApplyFn` template argument call
 * `applier(provider.get())` whenever the value changes, allowing the running
 * protocol to react immediately without polling.
 *
 * @see AtomicField
 * @see SubRegistry
 */
struct ContextProvider
{
    /**
     * @brief Returns `true` when a context pointer has been registered.
     */
    inline bool hasCtx() noexcept
    {
        return ctx != nullptr;
    }

    /**
     * @brief Returns the stored context pointer; nullptr if not set.
     */
    inline void* get() noexcept
    {
        return ctx;
    }

    /**
     * @brief Registers the protocol context pointer.
     *
     * @param c  Raw pointer to the protocol instance (e.g. the EIGRP process).
     *           The caller must ensure the pointer outlives the registry field.
     */
    void set(void* c) noexcept
    {
        ctx = c;
    }

    /**
     * @brief Clears the stored context pointer, disabling live notifications.
     */
    void clear() noexcept
    {
        ctx = nullptr;
    }

private:
    void* ctx{nullptr}; ///< Raw pointer to the owning protocol process; null when inactive.
};

/**
 * @brief Lock-free config field backed by `std::atomic<T>`.
 * @ingroup CONFIG
 *
 * `AtomicField<T>` stores a value that can be read concurrently from any
 * thread without taking a lock. When a parent scope is set (via
 * `setMask()`), `load()` transparently falls through to the parent if the
 * local value is in `INHERIT` state.
 *
 * The optional template parameter `H` is a free function of type
 * `void(*)(void*)` (an @ref ApplyFn). When non-null, setting or unsetting the
 * field immediately invokes `H(contextProvider.get())` so the owning protocol
 * can react. The plain `nullptr` specialisation omits this plumbing.
 *
 * ## Concurrency Model
 * - `load()` / `set()` / `unset()` are lock-free (`memory_order_relaxed` /
 *   `memory_order_release`).
 * - `setMask()` is called once during construction and is not thread-safe.
 *
 * @tparam T  Value type. Must be trivially copyable and fit in `std::atomic<T>`.
 * @tparam H  Optional applier callback invoked on every value change.
 *            Use `nullptr` (default) when no live notification is needed.
 *
 * @see OptionalAtomicField
 * @see SubRegistry
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class AtomicField;

/// @brief `AtomicField` specialization without a live-notification applier.
template <typename T CONFIG_INDEX_PARAM>
class AtomicField<T CONFIG_INDEX_ARG(F), nullptr> : public AtomicFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    /**
     * @brief Reads the effective value, falling through to the parent if in INHERIT state.
     * @return The locally-set value, or the parent's value if not overridden.
     */
    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == FieldState::INHERIT)
            return base->load();

        return value.load(std::memory_order_relaxed);
    }

    /**
     * @brief Sets a local value and transitions this field to SET state.
     * @param v  New value to store.
     */
    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
        state.store(FieldState::CANNED, std::memory_order_release);
    }

    /**
     * @brief Clears the local override and reverts to INHERIT state.
     *
     * The field's effective value becomes the parent's value (or the default
     * if there is no parent).
     */
    inline void unset() noexcept
    {
        value.store(getDefault(), std::memory_order_relaxed);
        state.store(FieldState::INHERIT, std::memory_order_relaxed);
    }

    /**
     * @brief Sets the value to the default value.
     */
    void setDefault() noexcept
    {
        set(defaultValue);
        state.store(FieldState::CANNED);
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == SET).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    /// @brief Returns the applicable default (parent's default if a parent exists).
    T getDefault() noexcept
    {
        if (base) return base->defaultValue;
        else return defaultValue;
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Overrides the default value returned by `unset()`.
    void setDefault(T d) noexcept
    {
        defaultValue = d;
    }

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(const AtomicField* parent) noexcept
    {
        base = parent;
    }

    std::atomic<T> value{T{}};                     ///< Stored value; valid only when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT}; ///< Whether a local override is active.
    const AtomicField* base{nullptr};              ///< Parent field for inheritance; null at root.
    T defaultValue{T{}};                           ///< Value returned after `unset()`.
};

/// @brief `AtomicField` specialization with a live-notification applier callback.
template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class AtomicField<T CONFIG_INDEX_ARG(F), H> : public AtomicFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H; ///< Callback invoked whenever the effective value changes.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to a shared @ref ContextProvider.
     *
     * @param p  Context provider shared with all other applier-enabled fields
     *           in the same @ref SubRegistry.
     */
    AtomicField(ContextProvider& p) noexcept
        : provider(p)
    {}

    /**
     * @brief Reads the effective value, falling through to the parent if in INHERIT state.
     * @return The locally-set value, or the parent's value if not overridden.
     */
    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == FieldState::INHERIT)
            return base->load();
        return value.load(std::memory_order_relaxed);
    }

    /**
     * @brief Sets a local value, transitions to SET state, and fires the applier if the value changed.
     * @param v  New value to store.
     */
    inline void set(T v) noexcept
    {
        bool apply = load() != v;
        value.store(v, std::memory_order_release);
        state.store(FieldState::CANNED, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    /**
     * @brief Reverts to INHERIT state and fires the applier if the effective value changed.
     *
     * After this call, `load()` returns the parent's value (or the default if no parent).
     */
    inline void unset() noexcept
    {
        T old = load();
        value.store(getDefault(), std::memory_order_relaxed);
        state.store(FieldState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    /**
     * @brief Sets the field to its default value.
     */
    void setDefault() noexcept
    {
        set(defaultValue);
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == CANNED).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    /// @brief Returns the applicable default (parent's default if a parent exists).
    T getDefault() noexcept
    {
        if (base) return base->defaultValue;
        else return defaultValue;
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Overrides the default value returned by `unset()`.
    void setDefault(T d) noexcept
    {
        defaultValue = d;
    }

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(const AtomicField* parent) noexcept
    {
        base = parent;
    }

    ContextProvider& provider;                        ///< Shared context used to fire the applier.
    std::atomic<T> value{T{}};                        ///< Stored value; valid only when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT}; ///< Whether a local override is active.
    const AtomicField* base{nullptr};                 ///< Parent field for inheritance; null at root.
    T defaultValue{T{}};                              ///< Value returned after `unset()`.
};

/**
 * @brief Lock-free config field that may be absent (no default value).
 * @ingroup CONFIG
 *
 * Identical to @ref AtomicField except that the field starts in an unset
 * state and `hasValue()` must be checked before calling `load()`. Suitable
 * for optional CLI commands whose absence has a different meaning from being
 * set to a zero-value (e.g. `router-id`, optional timers).
 *
 * @tparam T  Value type. Must be trivially copyable and fit in `std::atomic<T>`.
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see AtomicField
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class OptionalAtomicField;

/// @brief `OptionalAtomicField` specialization without a live-notification applier.
template <typename T CONFIG_INDEX_PARAM>
class OptionalAtomicField<T CONFIG_INDEX_ARG(F), nullptr> : public OptionalAtomicFieldFlag
{
public:
    using type = T;
#if USE_CONFIG_INDEX
    static constexpr auto field = F;
#endif

    /**
     * @brief Returns true when a locally-set or inherited value exists.
     *
     * Unlike @ref AtomicField, `OptionalAtomicField` has no default — a field
     * with no parent and no locally-set value returns false here.
     */
    inline bool hasValue() const noexcept
    {
        auto st = state.load(std::memory_order_release);
        if (st == FieldState::CANNED)
            return true;

        if (st == FieldState::INHERIT && base)
            return base->hasValue();

        return false;
    }

    /**
     * @brief Reads the effective value, falling through to the parent if in INHERIT state.
     *
     * @warning Calling `load()` when `hasValue()` is false is undefined behavior.
     * @return The locally-set value, or the parent's value if not overridden.
     */
    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == FieldState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }
        return value.load(std::memory_order_relaxed);
    }

    /**
     * @brief Sets a local value and transitions to SET state.
     * @param v  New value to store.
     */
    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
        state.store(FieldState::CANNED, std::memory_order_release);
    }

    /**
     * @brief Clears the local value and reverts to INHERIT state.
     *
     * After this call `hasValue()` returns false unless a parent is set.
     */
    inline void unset() noexcept
    {
        state.store(FieldState::INHERIT, std::memory_order_release);
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == CANNED).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    /**
     * @brief Marks the field as explicitly unset (UNSET state), disabling inheritance.
     *
     * After this call, `hasValue()` returns false even if a parent is present.
     */
    void setDefault() noexcept
    {
        state.store(FieldState::UNSET, std::memory_order_release);
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(const OptionalAtomicField* parent) noexcept
    {
        base = parent;
    }

    bool isSet = false;
    std::atomic<T> value{};                            ///< Stored value; only valid when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local value has been set.
    const OptionalAtomicField* base{nullptr};          ///< Parent field for inheritance; null at root.
};

/// @brief `OptionalAtomicField` specialization with a live-notification applier callback.
template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class OptionalAtomicField<T CONFIG_INDEX_ARG(F), H> : public OptionalAtomicFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H; ///< Callback invoked whenever the effective value changes.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to a shared @ref ContextProvider.
     *
     * @param provider  Context provider shared with all applier-enabled fields
     *                  in the same @ref SubRegistry.
     */
    OptionalAtomicField(ContextProvider& provider)
        : provider(provider)
    {}

    /**
     * @brief Returns true when a locally-set or inherited value exists.
     */
    inline bool hasValue() const noexcept
    {
        auto st = state.load(std::memory_order_release);
        if (st == FieldState::CANNED)
            return true;

        if (st == FieldState::INHERIT && base)
            return base->hasValue();

        return false;
    }

    /**
     * @brief Reads the effective value, falling through to the parent if in INHERIT state.
     *
     * @warning Calling `load()` when `hasValue()` is false is undefined behavior.
     * @return The locally-set value, or the parent's value if not overridden.
     */
    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == FieldState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }
        return value.load(std::memory_order_relaxed);
    }

    /**
     * @brief Sets a local value, transitions to SET state, and fires the applier if the value changed.
     * @param v  New value to store.
     */
    inline void set(T v) noexcept
    {
        bool apply = hasValue() || load() != v;
        value.store(v, std::memory_order_release);
        state.store(FieldState::CANNED, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    /**
     * @brief Clears the local value, reverts to INHERIT state, and fires the applier.
     *
     * The applier is called only if the field was previously SET (i.e. the
     * effective value visible to the protocol actually changed).
     */
    inline void unset() noexcept
    {
        if (state != FieldState::INHERIT)
        {
            state.store(FieldState::INHERIT, std::memory_order_release);
            if (provider.hasCtx()) applier(provider.get());
        }
    }

    /**
     * @brief Marks the field as explicitly unset (UNSET state) and fires the applier.
     *
     * After this call `hasValue()` returns false even if a parent is present.
     * The applier is fired only when the state actually changes.
     */
    void setDefault() noexcept
    {
        if (state != FieldState::UNSET)
        {
            state.store(FieldState::UNSET, std::memory_order_release);
            if (provider.hasCtx()) applier(provider.get());
        }
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == CANNED).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(const OptionalAtomicField* parent) noexcept
    {
        base = parent;
    }

    ContextProvider& provider;                         ///< Shared context used to fire the applier.
    std::atomic<T> value{};                            ///< Stored value; only valid when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local value has been set.
    const OptionalAtomicField* base{nullptr};          ///< Parent field for inheritance; null at root.
};

/**
 * @brief Mutex-protected config field for non-atomic value types.
 * @ingroup CONFIG
 *
 * Use `ListField<T>` when `T` cannot be held in a `std::atomic` — for
 * example, `std::string`, `std::vector`, or other heap-allocated types.
 * Reads and writes go through `withRead()` / `withWrite()` lambdas that hold
 * `mu` for the duration of the call.
 *
 * Like @ref AtomicField, `ListField` supports parent-inheritance and an
 * optional `ApplyFn` callback.
 *
 * ## Concurrency Model
 * - `mu` is a reference to the `SubRegistry::mu` shared by all `ListField`
 *   and `ValueField` members in the same registry struct.
 * - `withRead()` and `withWrite()` both take a `std::lock_guard` on `mu`.
 *   Do not call one from inside the other.
 *
 * @tparam T  Value type (heap-allocated or non-atomic-capable).
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see AtomicField
 * @see SubRegistry
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class ListField;

/// @brief `ListField` specialization without a live-notification applier.
template <typename T CONFIG_INDEX_PARAM>
class ListField<T CONFIG_INDEX_ARG(F), nullptr> : public ListFieldFlag
{
public:
    using type = std::vector<T>;
    using node = T;
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to the registry's shared mutex.
     *
     * @param m  Mutex shared with all other `ListField` and `ValueField`
     *           members in the same @ref SubRegistry struct.
     */
    ListField(std::mutex& m) noexcept
        : mu(m)
    {}

    /**
     * @brief Invokes `fn` with a const reference to the effective value under the shared lock.
     *
     * Falls through to the parent's `withRead` if the field is in INHERIT state.
     *
     * @tparam Fn  Callable of the form `void(const T&)`.
     */
    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        std::lock_guard<std::mutex> lock(mu);
        return std::forward<Fn>(fn)(value);
    }

    /**
     * @brief Invokes `fn` with a mutable reference to the local value under the shared lock.
     *
     * Transitions the field to SET state before calling `fn`. The applier is
     * not fired by this variant — use the applier-enabled specialization if
     * live notifications are needed.
     *
     * @tparam Fn  Callable of the form `void(T&)`.
     */
    template <typename Fn>
    void withWrite(Fn&& fn)
    {
        std::lock_guard<std::mutex> lk(mu);
        std::forward<Fn>(fn)(value);
    }

    std::mutex& mu; ///< Shared mutex; held during all reads and writes.

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(ListField* parent)
    {
        (void)parent;
    }

    std::vector<T> value{};                                         ///< Guarded value; valid when state == SET.
};

/// @brief `ListField` specialization with a live-notification applier callback.
template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class ListField<T CONFIG_INDEX_ARG(F), H> : public ListFieldFlag
{
public:
    using type = std::vector<T>;
    using node = T;
    static constexpr ApplyFn applier = H; ///< Callback invoked whenever the effective value changes.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to a shared context provider and mutex.
     *
     * @param provider  Context provider shared with all applier-enabled fields
     *                  in the same @ref SubRegistry.
     * @param m         Mutex shared with all `ListField` members in the same struct.
     */
    ListField(ContextProvider& provider, std::mutex& m)
        : provider(provider),
          mu(m)
    {}

    /**
     * @brief Fires the applier under the shared lock if a context is registered.
     *
     * Called internally after a write that changes the effective value.
     */
    void runApply()
    {
        if (!provider.hasCtx())
            return;
        std::lock_guard<std::mutex> lk(mu);
        applier(provider.get());
    }

    /**
     * @brief Invokes `fn` with a const reference to the effective value under the shared lock.
     *
     * Falls through to the parent's `withRead` if the field is in INHERIT state.
     *
     * @tparam Fn  Callable of the form `void(const T&)`.
     */
    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        std::lock_guard<std::mutex> lk(mu);
        std::forward<Fn>(fn)(value);
    }

    /**
     * @brief Invokes `fn` with a mutable reference to the local value, then fires the applier if the value changed.
     *
     * Transitions the field to SET state before calling `fn`. The applier is
     * fired after the lock is released if `fn` produced a different value.
     *
     * @tparam Fn  Callable of the form `void(T&)`.
     */
    template <typename Fn>
    void withWrite(Fn&& fn)
    {
        bool runApplier{false};
        {
            std::lock_guard<std::mutex> lk(mu);
            runApplier = std::forward<Fn>(fn)(value);
        }

        if (runApplier && provider.hasCtx())
            applier(provider.get());
    }

    std::mutex& mu; ///< Shared mutex; held during all reads and writes.

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(ListField* parent)
    {
        (void)parent;
    }

    ContextProvider& provider;                         ///< Shared context used to fire the applier.
    std::vector<T> value{};                            ///< Guarded value; valid when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local override is active.
    ListField* base{nullptr};                         ///< Parent field for inheritance; null at root.
};

/**
 * @brief Mutex-protected config field for non-atomic value types that may be absent.
 * @ingroup CONFIG
 *
 * Combines the optional (nullable) semantics of @ref OptionalAtomicField with
 * the mutex-guarded storage of @ref ListField. Use when `T` is not trivially
 * copyable and its absence has a distinct meaning from a zero-value.
 *
 * `hasValue()` must be checked before calling `load()`.
 *
 * ## Concurrency Model
 * Same as @ref ListField — `mu` is a reference to the owning `SubRegistry`'s
 * shared mutex.
 *
 * @tparam T  Value type (non-trivially copyable or too large for `std::atomic`).
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see ListField
 * @see OptionalAtomicField
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class ValueField;

/// @brief `ValueField` specialization without a live-notification applier.
template <typename T CONFIG_INDEX_PARAM>
class ValueField<T CONFIG_INDEX_ARG(F), nullptr> : public ValueFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to the registry's shared mutex.
     *
     * @param m  Mutex shared with all other `ListField` and `ValueField`
     *           members in the same @ref SubRegistry struct.
     */
    ValueField(std::mutex& m) noexcept
        : mu(m)
    {}

    /**
     * @brief Returns true when a locally-set or inherited value exists.
     */
    inline bool hasValue() const noexcept
    {
        auto st = state.load(std::memory_order_relaxed);
        if (st == FieldState::CANNED)
            return true;

        if (st == FieldState::INHERIT && base)
            return base->hasValue();

        return false;
    }

    /**
     * @brief Reads the effective value under the shared lock, falling through to the parent if in INHERIT state.
     *
     * @warning Calling `load()` when `hasValue()` is false is undefined behavior.
     * @return A copy of the locally-set value, or the parent's value if not overridden.
     */
    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == FieldState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    /**
     * @brief Stores a local value under the shared lock and transitions to SET state.
     * @param v  New value to store.
     */
    inline void set(T v) noexcept
    {
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
        state.store(FieldState::CANNED, std::memory_order_release);
    }

    /**
     * @brief Reverts to INHERIT state without clearing the stored bytes.
     *
     * After this call `hasValue()` returns false unless a parent is set.
     * The raw `value` bytes are left intact; they are not observable until
     * the field transitions back to SET.
     */
    inline void unset() noexcept
    {
        state.store(FieldState::INHERIT, std::memory_order_release);
    }

    /**
     * @brief Marks the field as explicitly unset (UNSET state), disabling inheritance.
     *
     * After this call `hasValue()` returns false even if a parent is present.
     */
    inline void setDefault() noexcept
    {
        state.store(FieldState::UNSET, std::memory_order_release);
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == CANNED).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    std::mutex& mu; ///< Shared mutex; held during all reads and writes.

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(ValueField* parent)
    {
        base = parent;
    }

    T value{};                                         ///< Guarded value; valid only when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local value has been set.
    ValueField* base{nullptr};                 ///< Parent field for inheritance; null at root.
};

/// @brief `ValueField` specialization with a live-notification applier callback.
template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class ValueField<T CONFIG_INDEX_ARG(F), H> : public ValueFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H; ///< Callback invoked whenever the effective value changes.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to a shared context provider and mutex.
     *
     * @param provider  Context provider shared with all applier-enabled fields
     *                  in the same @ref SubRegistry.
     * @param m         Mutex shared with all `ListField` members in the same struct.
     */
    ValueField(ContextProvider& provider, std::mutex& m)
        : provider(provider),
          mu(m)
    {}

    /**
     * @brief Returns true when a locally-set or inherited value exists.
     */
    inline bool hasValue() const noexcept
    {
        auto st = state.load(std::memory_order_relaxed);
        if (st == FieldState::CANNED)
            return true;

        if (st == FieldState::INHERIT && base)
            return base->hasValue();

        return false;
    }

    /**
     * @brief Reads the effective value under the shared lock, falling through to the parent if in INHERIT state.
     *
     * @warning Calling `load()` when `hasValue()` is false is undefined behavior.
     * @return A copy of the locally-set value, or the parent's value if not overridden.
     */
    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == FieldState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    /**
     * @brief Stores a local value under the shared lock, transitions to SET state, and fires the applier if the value changed.
     * @param v  New value to store.
     */
    inline void set(T v) noexcept
    {
        bool apply = load() != v;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
        state.store(FieldState::CANNED, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    /**
     * @brief Reverts to INHERIT state and fires the applier if the effective value changed.
     *
     * After this call `hasValue()` returns false unless a parent is set.
     */
    inline void unset() noexcept
    {
        T old = load();
        state.store(FieldState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == CANNED).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    std::mutex& mu; ///< Shared mutex; held during all reads and writes.

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(ValueField* parent)
    {
        base = parent;
    }

    ContextProvider& provider;                         ///< Shared context used to fire the applier.
    T value{};                                         ///< Guarded value; valid only when state == SET.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local value has been set.
    ValueField* base{nullptr};                 ///< Parent field for inheritance; null at root.
};

/**
 * @brief Registry field that owns an ordered map of child config scopes.
 * @ingroup CONFIG
 *
 * `OwnedListField` stores a `std::unordered_map<K, Reference<T>>` of
 * child registry entries indexed by key `K` (e.g. AS number, area ID).
 * Like all registry fields it supports parent-inheritance: when in INHERIT
 * state, `get()` returns the parent's map.
 *
 * Mutations go through `getMutable()`, which transitions the field to SET
 * state and returns a writable reference to the local children map.
 * @ref RegistryDatabase::emplaceBack is the recommended entry point.
 *
 * ## Lifecycle & Ownership
 * Each `Reference<T>` in `children` increments the corresponding bucket
 * slot's refcount. Erasing or clearing the map releases those references.
 *
 * ## Callback variant
 * When the optional `H` template parameter is set to an `ApplyFn`, the field
 * fires `H(ctx)` after every insert (via `emplaceBack`) and every `erase()` /
 * `clear()`. The context pointer is registered with the owning
 * `SubRegistry::context()` at protocol startup.
 *
 * @tparam T  Registry struct type of the child entries.
 * @tparam K  Key type used to index children (must be hashable).
 * @tparam H  Optional `ApplyFn` callback fired on every structural change.
 *            Defaults to `nullptr` (no notification).
 *
 * @see RegistryDatabase::emplaceBack
 * @see Reference
 */
template <typename T, typename K CONFIG_INDEX_PARAM, auto H = nullptr>
class OwnedListField;

/// @brief `OwnedListField` specialization without a live-notification applier.
template <typename T, typename K CONFIG_INDEX_PARAM>
class OwnedListField<T, K CONFIG_INDEX_ARG(F), nullptr> : public OwnedListFieldFlag
{
public:
    using type = T; ///< Child entry type.
    using key = K;  ///< Key type used to look up children.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Inserts a new child entry keyed by `k`, or returns the existing one.
     *
     * If an entry for `k` already exists it is returned unchanged; otherwise a
     * default-constructed `T` is inserted and returned.
     *
     * @param k  Key identifying the child entry.
     * @return Reference to the (new or existing) child entry.
     */
    T& emplaceBack(const K& k) noexcept
    {
        auto [it, ok] = children.try_emplace(k);
        return it->second;
    }

    /**
     * @brief Finds a child entry by key.
     *
     * @param k  Key to search for.
     * @return Const iterator to the matching entry, or `end()` if not found.
     */
    inline std::unordered_map<key, type>::iterator find(const K& k) noexcept
    {
        return children.find(k);
    }

    /**
     * @brief Returns the past-the-end iterator for the local children map.
     */
    inline std::unordered_map<key, type>::iterator end() noexcept
    {
        return children.end();
    }

    /**
     * @brief Returns the begin iterator for the local children map.
     */
    inline std::unordered_map<key, type>::iterator begin() noexcept
    {
        return children.begin();
    }

    /**
     * @brief Returns a reference to the local children map.
     *
     * @return Reference to the `unordered_map<K, T>`.
     */
    inline std::unordered_map<key, type>& get() noexcept
    {
        return children;
    }

    /**
     * @brief Returns a const reference to the local children map.
     *
     * @return Const reference to the `unordered_map<K, T>`.
     */
    inline const std::unordered_map<key, type>& get() const noexcept
    {
        return children;
    }

    /**
     * @brief Returns a bool depending on if the key exists in the children map.
     *
     * @return returns true if the key was found, otherwise false.
     */
    inline bool contains(key& k) const noexcept
    {
        return children.contains(k);
    }

    /**
     * @brief Removes the child entry identified by `k` from the local map.
     *
     * @param k  Key of the entry to remove.
     */
    inline void erase(const K& k) noexcept
    {
        children.erase(k);
    }

    /**
     * @brief Removes all child entries.
     */
    inline void clear() noexcept
    {
        children.clear();
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;
    template <typename...>
    friend class RegistryDatabase;

    void setMask(OwnedListField* parent) noexcept { (void)parent; }

    std::unordered_map<key, type> children{}; ///< Locally-owned child entries.
};

/// @brief `OwnedListField` specialization with a live-notification applier callback.
template <typename T, typename K CONFIG_INDEX_PARAM, ApplyFn H>
class OwnedListField<T, K CONFIG_INDEX_ARG(F), H> : public OwnedListFieldFlag
{
public:
    using type = T; ///< Child entry type.
    using key = K;  ///< Key type used to look up children.
    static constexpr ApplyFn applier = H; ///< Callback invoked after every structural change.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to a shared @ref ContextProvider.
     *
     * @param p  Context provider shared with all applier-enabled fields in the
     *           same @ref SubRegistry.
     */
    explicit OwnedListField(ContextProvider& p) noexcept
        : provider(p)
    {}

    /**
     * @brief Fires the applier if a context pointer has been registered.
     *
     * Called internally by `erase()`, `clear()`, and @ref RegistryDatabase::emplaceBack
     * after every structural change to the children map.
     */
    void notifyChanged() noexcept
    {
        if (provider.hasCtx())
            applier(provider.get());
    }

    /**
     * @brief Inserts a new child entry keyed by `k`, or returns the existing one.
     *
     * If the entry is newly inserted, fires the applier callback via
     * @ref notifyChanged so that the owning protocol process can react to the
     * structural change (e.g., re-evaluate neighbor configuration).
     *
     * @param k  Key identifying the child entry.
     * @return Reference to the (new or existing) child entry.
     */
    T& emplaceBack(const K& k) noexcept
    {
        auto [it, ok] = children.try_emplace(k);
        if (ok) notifyChanged();
        return it->second;
    }

    /**
     * @brief Finds a child entry by key.
     *
     * @param k  Key to search for.
     * @return Const iterator to the matching entry, or `end()` if not found.
     */
    inline std::unordered_map<key, type>::const_iterator find(const K& k) const noexcept
    {
        return children.find(k);
    }

    /**
     * @brief Returns the past-the-end iterator for the local children map.
     */
    inline std::unordered_map<key, type>::const_iterator end() const noexcept
    {
        return children.end();
    }

    /**
     * @brief Returns the begin iterator for the local children map.
     */
    inline std::unordered_map<key, type>::const_iterator begin() const noexcept
    {
        return children.begin();
    }

    /**
     * @brief Returns a reference to the local children map.
     *
     * @return Reference to the `unordered_map<K, T>`.
     */
    inline std::unordered_map<key, type>& get() noexcept
    {
        return children;
    }

    /**
     * @brief Returns a const reference to the local children map.
     *
     * @return Const reference to the `unordered_map<K, T>`.
     */
    inline const std::unordered_map<key, type>& get() const noexcept
    {
        return children;
    }

    /**
     * @brief Returns a bool depending on if the key exists in the children map.
     *
     * @return returns true if the key was found, otherwise false.
     */
    inline bool contains(key& k) const noexcept
    {
        return children.contains(k);
    }

    /**
     * @brief Removes the child entry identified by `k` and fires the applier.
     *
     * @param k  Key of the entry to remove.
     */
    inline void erase(const K& k) noexcept
    {
        children.erase(k);
        notifyChanged();
    }

    /**
     * @brief Removes all child entries and fires the applier.
     */
    inline void clear() noexcept
    {
        children.clear();
        notifyChanged();
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;
    template <typename...>
    friend class RegistryDatabase;

    void setMask(OwnedListField* parent) noexcept
    {
        (void)parent;
    }

    ContextProvider& provider; ///< Shared context used to fire the applier.
    std::unordered_map<key, type> children{}; ///< Locally-owned child entries.
};

/**
 * @brief Wrapper that makes a value participate in config field storage but be
 *        excluded from equality comparisons between list entries.
 *
 * When a config list entry (node) contains a field that should not act as a
 * match key — for example, a description string or a metadata tag — wrapping
 * it in `IgnoreCompare<T>` causes @ref compareTuple to skip it during the
 * duplicate-check that guards `setListEntry`.  The value is still stored and
 * accessible via implicit conversion to `T`.
 *
 * @tparam T  Underlying value type to store.
 *
 * @see IgnoreCompareFlag
 * @see IsIgnoreCompare
 */
template <typename T>
struct IgnoreCompare : public IgnoreCompareFlag
{
    using type = T;
    T value;

    IgnoreCompare() = default;
    IgnoreCompare(const T& v) : value(v) {};
    IgnoreCompare(T&& v) : value(std::move(v)) {}

    // Implicit conversion to T
    operator T&() { return value; }
    operator const T&() { return value; }

    // Assignment
    IgnoreCompare& operator=(const T& v) { value = v; return *this; }
    IgnoreCompare& operator=(T&& v) { value = std::move(v); return *this; }
};

// FIELD CONCEPTS

/**
 * @brief Satisfied by any field type that exposes a static `applier` function pointer.
 *
 * When a field satisfies `RequiresContext`, the owning @ref SubRegistry must
 * supply a @ref ContextProvider so that the applier can be called when the
 * field value changes.
 *
 * @tparam T  Field type to test.
 */
template <typename T>
concept RequiresContext =
    requires { T::applier; };

/**
 * @brief Satisfied by required (non-optional) atomic config fields.
 *
 * Implies `IsFieldBase<T>`, derivation from `AtomicFieldFlag`, and *not*
 * derivation from `OptionalAtomicFieldFlag`.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsAtomicField =
    IsFieldBase<T> &&
    std::derived_from<T, AtomicFieldFlag> &&
    (!std::derived_from<T, OptionalAtomicFieldFlag>);

/**
 * @brief Satisfied by optional atomic config fields (`hasValue()` may return false).
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsOptionalAtomicField =
    IsFieldBase<T> &&
    std::derived_from<T, OptionalAtomicFieldFlag>;

/**
 * @brief Satisfied by reference-container fields that hold a child scope handle.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsRefContainer =
    IsFieldBase<T> &&
    std::derived_from<T, RefContainerFieldFlag>;

/**
 * @brief Satisfied by reference-container fields that carry an embedded index.
 *
 * A subset of @ref IsRefContainer where `T::hasRefIndex` is `true`, used
 * to select index-aware code paths in @ref SubRegistry.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsIndexedRefContainer =
    IsRefContainer<T> &&
    T::hasRefIndex;

/**
 * @brief Satisfied by reference-container fields without an embedded index.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsUnindexedRefContainer =
    IsRefContainer<T> &&
    !T::hasRefIndex;

/**
 * @brief Satisfied by mutex-guarded, non-optional value config fields.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsListField =
    IsFieldBase<T> &&
    std::derived_from<T, ListFieldFlag>;

/**
 * @brief Satisfied by mutex-guarded, optional value config fields.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsValueField =
    IsFieldBase<T> &&
    std::derived_from<T, ValueFieldFlag>;

/**
 * @brief Satisfied by owned-list fields that hold a keyed map of child scopes.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsOwnedListField =
    IsFieldBase<T> &&
    std::derived_from<T, OwnedListFieldFlag>;

/**
 * @brief Satisfied by compare fields that hold hold a tuple field type.
 *
 * @tparam T  type to test.
 */
template <typename T>
concept IsIgnoreCompare =
    std::derived_from<T, IgnoreCompareFlag>;
}

#endif // REGISTRY_TYPES_HPP

