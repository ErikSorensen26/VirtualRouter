/**
 * @file RegistryTypes.hpp
 * @brief Core field types, flag traits, and C++ concepts used by the registry.
 * @ingroup CONFIG
 */

/**
 * @defgroup CONFIG Configuration Registry
 * @brief Type-safe, hierarchical configuration storage for all protocol and system scopes.
 *
 * Provides the field types (`AtomicField`, `ValueField`, `ListField`, etc.), the
 * `SubRegistry` template that owns a fixed set of those fields, and the accessor
 * classes (`AtomicFieldAccessor`, etc.) that expose live-notification semantics
 * to the CLI and protocol engines.
 *
 * ## Architectural Role
 * Configuration is separated from behaviour. Registry types hold values; protocol
 * processes subscribe via `ContextProvider` and receive callbacks whenever a field
 * that affects them changes. This keeps the CLI layer decoupled from the protocol
 * implementation.
 *
 * ## Subdirectory Groups
 * - @ref CONFIG_GLOBAL  — global and per-VRF configuration schemas
 * - @ref CONFIG_INTERFACE — per-interface configuration schemas
 * - @ref CONFIG_POLICY — policy-map, route-map, ACL, and prefix-list schemas
 */


#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>
#include <optional> // IWYU pragma: keep
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

// TYPE ALIASES
using ApplyFn = void (*)(void* ctx); ///< Callback signature for live-notification appliers.

template <typename ...Fields>
struct FieldTuple;
template <typename Base, typename ENUM, ApplyFn H, typename Fields>
class SubRegistry;

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
 * @brief Flag type that marks a field as a reference-container (sub-scope).
 * @ingroup CONFIG
 *
 * Used by @ref RegistryContainer to satisfy the @ref IsRefContainer concept.
 */
struct RefContainerFieldFlag {};

/**
 * @brief Flag type that markes a field as an optional reference-container (sub-scope).
 * @ingroup CONFIG
 * 
 * Used by @ref OptionalRegistryContainer to satisfy the @ref IsOptionalRefContainer concept.
 */
struct OptionalRefContainerFieldFlag {};

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

/**
 * @brief Carries the optional live-notification applier for a field type.
 * @ingroup CONFIG
 *
 * Every field template takes an `H` parameter that is either `nullptr` or an
 * @ref ApplyFn. Deriving from `ApplierHolder<Flag, H>` keeps the two cases in a
 * single class definition: the primary template contributes nothing but the
 * flag base, while the `ApplyFn` partial specialization adds the static
 * `applier` member that @ref RequiresContext detects.
 *
 * `applier` must be public. @ref RequiresContext is a namespace-scope concept
 * with no friendship, and concept satisfaction honours access control, so a
 * private or protected `applier` makes `requires { T::applier; }` silently
 * false and disables every applier-firing branch that guards on it.
 *
 * @tparam Flag  Field-category flag base (e.g. `AtomicFieldFlag`).
 * @tparam H     `nullptr` for no notification, or an `ApplyFn` to install.
 */
template <typename Flag, auto H>
struct ApplierHolder : Flag {};

/// @brief `ApplierHolder` specialization that installs the applier callback.
template <typename Flag, ApplyFn H>
struct ApplierHolder<Flag, H> : Flag
{
    static constexpr ApplyFn applier = H; ///< Callback invoked whenever the effective value changes.
};

// FIELD CONCEPTS

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
    requires { typename T::type; };

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
 * @brief Satisfied by optional reference-container fields that hold a child scope handle.
 *
 * @tparam T  Type to test.
 */
template <typename T>
concept IsOptionalRefContainer =
    IsFieldBase<T> &&
    std::derived_from<T, OptionalRefContainerFieldFlag>;

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
class AtomicField : public ApplierHolder<AtomicFieldFlag, H>
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER
    void setMask(const AtomicField* p) noexcept { mask = p; }
private:
    template <typename, typename, ApplyFn, typename>
    friend class SubRegistry;
    template <IsAtomicField>
    friend class AtomicFieldAccessor;

    std::atomic<T> value{T{}};                          ///< Stored value; authoritative when state == CANNED.
    std::atomic<FieldState> state{FieldState::INHERIT}; ///< Whether a local override is active.
    const AtomicField* mask = nullptr;
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
class OptionalAtomicField : public ApplierHolder<OptionalAtomicFieldFlag, H>
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER
    void setMask(const OptionalAtomicField* p) noexcept { mask = p; }
private:
    template <typename, typename, ApplyFn, typename>
    friend class SubRegistry;
    template <IsOptionalAtomicField>
    friend class OptionalAtomicFieldAccessor;

    std::atomic<T> value{};                             ///< Stored value; authoritative when state == CANNED.
    std::atomic<FieldState> state{FieldState::INHERIT}; ///< Whether a local value has been set.
    const OptionalAtomicField* mask = nullptr;
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
 * Same as @ref ListField — the mutex lives in the owning `SubRegistry` and is
 * handed to the accessor; the field stores only the value pointer and its
 * inheritance state. The destructor deletes the value without locking.
 *
 * @tparam T  Value type (non-trivially copyable or too large for `std::atomic`).
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see ListField
 * @see OptionalAtomicField
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class ValueField : public ApplierHolder<ValueFieldFlag, H>
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER
    void setMask(const ValueField* p) noexcept { mask = p; }

    // value is a raw owning pointer; the last set() has no other owner to free it.
    // Destruction does not take the registry mutex, so the owning SubRegistry must
    // outlive every thread that can reach this field through an accessor.
    ~ValueField() { delete value.load(std::memory_order_relaxed); }
private:
    template <typename, typename, ApplyFn, typename>
    friend class SubRegistry;
    template <IsValueField>
    friend class ValueFieldAccessor;

    std::atomic<T*> value = nullptr;                     ///< Guarded value; authoritative when state == CANNED.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local value has been set.
    const ValueField* mask = nullptr;
};

/**
 * @brief Mutex-protected config field for non-atomic value types.
 * @ingroup CONFIG
 *
 * Use `ListField<T>` when `T` cannot be held in a `std::atomic` — for
 * example, `std::string`, `std::vector`, or other heap-allocated types.
 * Reads and writes go through the accessor's `withRead()` / `withWrite()`
 * lambdas, which hold the owning registry's mutex for the duration of the call.
 *
 * Unlike @ref AtomicField, `ListField` does not participate in parent
 * inheritance: it has no `state` or `mask` member, and an unwritten list simply
 * holds a null pointer. It does support the optional `ApplyFn` callback.
 *
 * ## Concurrency Model
 * - The mutex lives in the owning `SubRegistry` and is handed to
 *   @ref ListFieldAccessor on construction; the field itself stores only the
 *   value pointer. All `ListField` and `ValueField` members of the same
 *   registry share that one mutex.
 * - `withRead()` and `withWrite()` both take a `std::lock_guard` on it.
 *   Do not call one from inside the other.
 * - The destructor deletes the list without locking; see the ownership note below.
 *
 * @tparam T  Value type (heap-allocated or non-atomic-capable).
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see AtomicField
 * @see SubRegistry
 */
template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class ListField : public ApplierHolder<ListFieldFlag, H>
{
public:
    using type = std::vector<T>;
    using node = T;
    CONFIG_INDEX_MEMBER

    // value is a raw owning pointer; the list withWrite() allocates has no other owner.
    // Destruction does not take the registry mutex, so the owning SubRegistry must
    // outlive every thread that can reach this field through an accessor.
    ~ListField() { delete value.load(std::memory_order_relaxed); }
private:
    template <typename, typename, ApplyFn, typename>
    friend class SubRegistry;
    template <IsListField>
    friend class ListFieldAccessor;

    std::atomic<std::vector<T>*> value = nullptr; ///< Guarded value; null until the first withWrite().
};

/**
 * @brief Registry field that owns an ordered map of child config scopes.
 * @ingroup CONFIG
 *
 * `OwnedListField` stores a `std::unordered_map<K, T*>` of child registry
 * entries indexed by key `K` (e.g. AS number, area ID). Unlike @ref AtomicField
 * it does not participate in parent inheritance: it has no `state` or `mask`
 * member, and the map is always the field's own.
 *
 * Mutations go through @ref OwnedListFieldAccessor — `emplaceBack()` inserts or
 * returns an existing child, and `erase()` / `clear()` remove them.
 *
 * ## Lifecycle & Ownership
 * The field owns its children outright. `delFn` is installed on the first
 * `emplaceBack()` and is what frees an entry, so it is called on `erase()`,
 * `clear()`, and for every remaining child in the destructor. Copying is
 * deleted to keep that ownership single.
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
class OwnedListField : public ApplierHolder<OwnedListFieldFlag, H>
{
public:
    using type = T; ///< Child entry type.
    using key = K;  ///< Key type used to look up children.
    CONFIG_INDEX_MEMBER

    ~OwnedListField() {
        for (auto& [k, v] : children)
            if (delFn) delFn(v);
    }
    OwnedListField(const OwnedListField&) = delete;
    OwnedListField& operator=(const OwnedListField&) = delete;
    OwnedListField() = default;
private:
    template <typename, typename, ApplyFn, typename>
    friend class SubRegistry;
    template <IsOwnedListField>
    friend class OwnedListFieldAccessor;

    std::unordered_map<key, type*> children{}; ///< Pointer-owning child entries; ownership managed via delFn.
    void(*delFn)(type*) = nullptr; ///< Deleter set on first emplaceBack; called in destructor and erase.
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
    operator const T&() const { return value; }

    // Assignment
    IgnoreCompare& operator=(const T& v) { value = v; return *this; }
    IgnoreCompare& operator=(T&& v) { value = std::move(v); return *this; }
};
}

#endif // REGISTRY_TYPES_HPP

