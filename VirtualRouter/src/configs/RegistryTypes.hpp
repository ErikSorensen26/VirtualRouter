/**
 * @file RegistryTypes.hpp
 * @brief Core field types, flag traits, and C++ concepts used by the registry.
 */

// TODO finish doxy (the whole file...)

#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>
#include <utility>
#include <unordered_map>

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

template <typename T>
class Reference;

template <typename ENUM, typename... Fields>
class SubRegistry;

// TYPE ALIASES

using ApplyFn  = void (*)(void* ctx); ///< Callback signature for live-notification appliers.

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
 * Used by @ref ReferenceContainer to satisfy the @ref IsRefContainer concept.
 */
struct RefContainerFieldFlag {};

/**
 * @brief Flag type that marks a field as a mutex-guarded value config field.
 * @ingroup CONFIG
 *
 * Types derived from `ValueFieldFlag` satisfy the @ref IsValueField concept.
 */
struct ValueFieldFlag {};

/**
 * @brief Flag type that marks a field as an optional mutex-guarded value config field.
 * @ingroup CONFIG
 *
 * Types derived from `OptionalValueFieldFlag` satisfy the @ref IsOptionalValueField concept.
 */
struct OptionalValueFieldFlag {};

/**
 * @brief Flag type that marks a field as an owned map of child registry entries.
 * @ingroup CONFIG
 *
 * Types derived from `OwnedListFieldFlag` satisfy the @ref IsOwnedListField concept.
 */
struct OwnedListFieldFlag {};

// CONCEPTS

/**
 * @brief Satisfied by any registry field type that exposes a nested `::type` alias.
 *
 * Every field class in the registry (`AtomicField`, `ValueField`, etc.) must
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
enum class MaskState : uint8_t
{
    INHERIT, ///< Field defers to its parent scope; no local override.
    SET      ///< Field has a locally-set value that shadows the parent.
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
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
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
        state.store(MaskState::SET, std::memory_order_release);
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
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
    }

    /**
     * @brief Returns `true` when the field has a locally-set value (state == SET).
     */
    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    /**
     * @brief Overrides the default value returned by `unset()`.
     *
     * @param d  New default value for this field.
     */
    void setDefault(T d) noexcept
    {
        defaultValue = d;
    }

    /**
     * @brief Sets the value to the default value.
     */
    void setDefault() noexcept
    {
        set(defaultValue);
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(const AtomicField* parent) noexcept
    {
        base = parent;
    }

    /// @brief Returns the applicable default (parent's default if a parent exists).
    T getDefault() noexcept
    {
        if (base) return base->defaultValue;
        else return defaultValue;
    }

    std::atomic<T> value{T{}};                     ///< Stored value; valid only when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT}; ///< Whether a local override is active.
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

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->load();
        return value.load(std::memory_order_relaxed);
    }

    inline void set(T v) noexcept
    {
        bool apply = load() != v;
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline void unset() noexcept
    {
        T old = load();
        value.store(getDefault(), std::memory_order_relaxed);
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    void setDefault(T d) noexcept
    {
        defaultValue = d;
    }

    void setDefault() noexcept
    {
        set(defaultValue);
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const AtomicField* parent) noexcept
    {
        base = parent;
    }

    T getDefault() noexcept
    {
        if (base) return base->defaultValue;
        else return defaultValue;
    }

    ContextProvider& provider;                        ///< Shared context used to fire the applier.
    std::atomic<T> value{T{}};                        ///< Stored value; valid only when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT}; ///< Whether a local override is active.
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

    inline bool hasValue() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return true;

        if (base)
            return base->hasValue();

        return false;
    }

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }
        return value.load(std::memory_order_relaxed);
    }

    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_release);
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const OptionalAtomicField* parent) noexcept
    {
        base = parent;
    }

    std::atomic<T> value{};                            ///< Stored value; only valid when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT};  ///< Whether a local value has been set.
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

    inline bool hasValue() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return true;

        if (base)
            return base->hasValue();

        return false;
    }

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }
        return value.load(std::memory_order_relaxed);
    }

    inline void set(T v) noexcept
    {
        bool apply = hasValue() || load() != v;
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline void unset() noexcept
    {
        bool apply = state == MaskState::SET;
        state.store(MaskState::INHERIT, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const OptionalAtomicField* parent) noexcept
    {
        base = parent;
    }

    ContextProvider& provider;                         ///< Shared context used to fire the applier.
    std::atomic<T> value{};                            ///< Stored value; only valid when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT};  ///< Whether a local value has been set.
    const OptionalAtomicField* base{nullptr};          ///< Parent field for inheritance; null at root.
};

/**
 * @brief Mutex-protected config field for non-atomic value types.
 * @ingroup CONFIG
 *
 * Use `ValueField<T>` when `T` cannot be held in a `std::atomic` — for
 * example, `std::string`, `std::vector`, or other heap-allocated types.
 * Reads and writes go through `withRead()` / `withWrite()` lambdas that hold
 * `mu` for the duration of the call.
 *
 * Like @ref AtomicField, `ValueField` supports parent-inheritance and an
 * optional `ApplyFn` callback.
 *
 * ## Concurrency Model
 * - `mu` is a reference to the `SubRegistry::mu` shared by all `ValueField`
 *   and `OptionalValueField` members in the same registry struct.
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
     * @param m  Mutex shared with all other `ValueField` and `OptionalValueField`
     *           members in the same @ref SubRegistry struct.
     */
    ValueField(std::mutex& m) noexcept
        : mu(m)
    {}

    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->withRead(std::forward<Fn>(fn));

        std::lock_guard<std::mutex> lock(mu);
        return std::forward<Fn>(fn)(value);
    }

    template <typename Fn>
    void withWrite(Fn&& fn)
    {
        state.store(MaskState::SET, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu);
        return std::forward<Fn>(fn)(value);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu);
        value = T{};
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
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

    T value{};                                         ///< Guarded value; valid when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT};  ///< Whether a local override is active.
    ValueField* base{nullptr};                         ///< Parent field for inheritance; null at root.
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
     * @param m         Mutex shared with all `ValueField` members in the same struct.
     */
    ValueField(ContextProvider& provider, std::mutex& m)
        : provider(provider),
          mu(m)
    {}

    void runApply()
    {
        if (!provider.hasCtx())
            return;
        std::lock_guard<std::mutex> lk(mu);
        applier(provider.get());
    }

    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->withRead(std::forward<Fn>(fn));

        std::lock_guard<std::mutex> lk(mu);
        std::forward<Fn>(fn)(value);
    }

    template <typename Fn>
    void withWrite(Fn&& fn)
    {
        bool runApplier{false};
        state.store(MaskState::SET, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(mu);
            auto old = value;
            std::forward<Fn>(fn)(value);
            runApplier = old != value;
        }

        if (runApplier && provider.hasCtx())
        {
            applier(provider.get());
        }
    }

    inline void unset() noexcept
    {
        bool apply = state.exchange(MaskState::INHERIT, std::memory_order_relaxed) == MaskState::SET;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = T{};
        }
        if (apply && provider.hasCtx())
            runApply();
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
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
    T value{};                                         ///< Guarded value; valid when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT};  ///< Whether a local override is active.
    ValueField* base{nullptr};                         ///< Parent field for inheritance; null at root.
};

/**
 * @brief Mutex-protected config field for non-atomic value types that may be absent.
 * @ingroup CONFIG
 *
 * Combines the optional (nullable) semantics of @ref OptionalAtomicField with
 * the mutex-guarded storage of @ref ValueField. Use when `T` is not trivially
 * copyable and its absence has a distinct meaning from a zero-value.
 *
 * `hasValue()` must be checked before calling `load()`.
 *
 * ## Concurrency Model
 * Same as @ref ValueField — `mu` is a reference to the owning `SubRegistry`'s
 * shared mutex.
 *
 * @tparam T  Value type (non-trivially copyable or too large for `std::atomic`).
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see ValueField
 * @see OptionalAtomicField
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class OptionalValueField;

/// @brief `OptionalValueField` specialization without a live-notification applier.
template <typename T CONFIG_INDEX_PARAM>
class OptionalValueField<T CONFIG_INDEX_ARG(F), nullptr> : public OptionalValueFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    /**
     * @brief Constructs the field, binding it to the registry's shared mutex.
     *
     * @param m  Mutex shared with all other `ValueField` and `OptionalValueField`
     *           members in the same @ref SubRegistry struct.
     */
    OptionalValueField(std::mutex& m) noexcept
        : mu(m)
    {}

    inline bool hasValue() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return true;

        if (base)
            return base->hasValue();

        return false;
    }

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    inline void set(T v) noexcept
    {
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
        state.store(MaskState::SET, std::memory_order_release);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_release);
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu; ///< Shared mutex; held during all reads and writes.

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(OptionalValueField* parent)
    {
        base = parent;
    }

    T value{};                                         ///< Guarded value; valid only when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT};  ///< Whether a local value has been set.
    OptionalValueField* base{nullptr};                 ///< Parent field for inheritance; null at root.
};

/// @brief `OptionalValueField` specialization with a live-notification applier callback.
template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class OptionalValueField<T CONFIG_INDEX_ARG(F), H> : public OptionalValueFieldFlag
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
     * @param m         Mutex shared with all `ValueField` members in the same struct.
     */
    OptionalValueField(ContextProvider& provider, std::mutex& m)
        : provider(provider),
          mu(m)
    {}

    inline bool hasValue() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return true;

        if (base)
            return base->hasValue();

        return false;
    }

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    inline void set(T v) noexcept
    {
        bool apply = load() != v;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline void unset() noexcept
    {
        T old = load();
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu; ///< Shared mutex; held during all reads and writes.

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(OptionalValueField* parent)
    {
        base = parent;
    }

    ContextProvider& provider;                         ///< Shared context used to fire the applier.
    T value{};                                         ///< Guarded value; valid only when state == SET.
    std::atomic<MaskState> state{MaskState::INHERIT};  ///< Whether a local value has been set.
    OptionalValueField* base{nullptr};                 ///< Parent field for inheritance; null at root.
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
 * @tparam T  Registry struct type of the child entries.
 * @tparam K  Key type used to index children (must be hashable).
 *
 * @see RegistryDatabase::emplaceBack
 * @see Reference
 */
template <typename T, typename K CONFIG_INDEX_PARAM>
class OwnedListField : public OwnedListFieldFlag
{
public:
    using type = std::unordered_map<K, Reference<T>>; ///< Container type for child entries.
    using key = K;                                     ///< Key type used to look up children.
    CONFIG_INDEX_MEMBER

    /**
     * @brief Returns a mutable reference to the local children map, transitioning to SET state.
     *
     * @return Mutable reference to the local `unordered_map<K, Reference<T>>`.
     */
    inline type& getMutable() noexcept
    {
        state = MaskState::SET;
        return children;
    }

    /**
     * @brief Finds a child entry by key.
     *
     * @param key  Key to search for.
     * @return Const iterator to the matching entry, or `end()` if not found.
     */
    const inline type::const_iterator find(const K& key) const noexcept
    {
        return children.find(key);
    }

    /**
     * @brief Returns the past-the-end iterator for the local children map.
     */
    const inline type::const_iterator end() const noexcept
    {
        return children.end();
    }

    /**
     * @brief Returns the begin iterator for the local children map.
     */
    const inline type::const_iterator begin() const noexcept
    {
        return children.begin();
    }

    /**
     * @brief Returns the effective children map, falling through to the parent in INHERIT state.
     *
     * @return Const reference to the local or inherited `unordered_map<K, Reference<T>>`.
     */
    inline const type& get() const noexcept
    {
        if (base && state == MaskState::INHERIT)
            return base->get();
        return children;
    }

    /**
     * @brief Removes the child entry identified by `key` from the local map.
     *
     * @param key  Key of the entry to remove.
     */
    inline void erase(const K& key) noexcept
    {
        children.erase(key);
    }

    /**
     * @brief Removes all child entries and reverts to INHERIT state.
     */
    inline void clear() noexcept
    {
        children.clear();
        state = MaskState::INHERIT;
    }

    /**
     * @brief Returns `true` when the field has locally-owned children (state == SET).
     */
    inline bool overridden() const noexcept
    {
        return state == MaskState::SET;
    }

private:
    template <typename ENUM, typename... Fields>
    friend class SubRegistry;
    template <typename...>
    friend class RegistryDatabase;

    /// @brief Links this field to its parent scope's field for inheritance.
    void setMask(OwnedListField* parent)
    {
        base = parent;
    }

    type children{};                   ///< Locally-owned child entries.
    MaskState state{MaskState::INHERIT}; ///< Whether local children have been set.
    OwnedListField* base{nullptr};     ///< Parent field for inheritance; null at root.
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
concept IsValueField =
    IsFieldBase<T> &&
    std::derived_from<T, ValueFieldFlag>;

/**
 * @brief Satisfied by mutex-guarded, optional value config fields.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsOptionalValueField =
    IsFieldBase<T> &&
    std::derived_from<T, OptionalValueFieldFlag>;

/**
 * @brief Satisfied by owned-list fields that hold a keyed map of child scopes.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsOwnedListField =
    IsFieldBase<T> &&
    std::derived_from<T, OwnedListFieldFlag>;
}

#endif // REGISTRY_TYPES_HPP

