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
#include <Any.hpp>

#include "configs/TupleSchema.hpp"

#define IGNOR(type) config::IgnoreCompare<type>

/**
 * @namespace config
 * @brief Typed, hierarchical configuration storage for all protocol and system scopes.
 *
 * Holds the field types (`AtomicField`, `ValueField`, `ListField`, etc.), the
 * `SubRegistry` template that owns a fixed set of those fields, and the
 * accessor classes that expose live-notification and validation semantics to
 * the CLI and protocol engines. See @ref CONFIG for the full subsystem
 * description.
 */
namespace config
{
// FORWARD DECLARATIONS

template <typename...>
class RegistryDatabase;

// TYPE ALIASES
using Context = utils::Any;
template <typename T>
using ApplyFn = void (*)(Context&, T&); ///< Callback signature for live-notification appliers.
template <typename T>
using OptApplyFn = void (*)(Context&, T*); ///< Callback signature for live-notification appliers.
template <typename T>
using ListApplyFn = void (*)(Context&, T&, bool); ///< Callback signature for live-notifiaction appliers for owned lists.
template <typename T, typename K>
using OwnedApplyFn = void (*)(Context&, T*, const K&); ///< Callback signature for live-notifiaction appliers for owned lists.
template <typename T>
using ValidateFn = bool (*)(Context& ctx, T& val); ///< Callback signature for validators.

/// @brief Command-tree node index of a field never written from the CLI.
inline constexpr uint32_t NO_COMMAND_INDEX = 0xFFFFFFFFu;

template <typename ...Fields>
struct FieldTuple;
template <typename Base, typename ENUM, typename Fields>
class SubRegistry;

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
 * @brief No-op base: primary template for when `V` is not a `ValidateFn<T>` (no validator configured).
 * @ingroup CONFIG
 */
template <typename Flag, typename T, auto V>
struct ValidatorHolder : Flag
{
    template <typename S, auto EF>
    ValidatorHolder(std::in_place_type_t<S>, std::in_place_index_t<EF>) {};
};

/// @brief `ValidatorHolder` specialization that installs the validator callback.
template <typename Flag, typename T, auto V>
    requires std::is_same_v<decltype(V), ValidateFn<T>>
struct ValidatorHolder<Flag, T, V> : Flag
{
    template <typename S, auto EF>
    ValidatorHolder(std::in_place_type_t<S>, std::in_place_index_t<EF>)
        : validator(+[](Context& s, T& type) { return Context::cast<S*>(s)->template runValidator<static_cast<typename S::type>(EF)>(type); }) {}
    ValidateFn<T> validator;
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    static constexpr ValidateFn<T> validatorImpl = V;
};

/**
 * @brief No-op base: primary template for when `H` is not an `ApplyFn<T>` (no applier configured).
 * @ingroup CONFIG
 *
 * Note this primary template does not chain to @ref ValidatorHolder; only
 * the specialization below (when `H` does match) does. A field wanting a
 * validator without an applier still needs `H`'s type to select that
 * specialization for `V` to take effect.
 */
template <typename Flag, typename T, auto H, auto V> // applier, validator
struct CallbackHolder : Flag
{
    template <typename S, auto EF>
    CallbackHolder(std::in_place_type_t<S>, std::in_place_index_t<EF>) {};
};

/// @brief `CallbackHolder` specialization that installs the callback.
template <typename Flag, typename T, auto H, auto V>
    requires std::is_same_v<decltype(H), ApplyFn<T>>
struct CallbackHolder<Flag, T, H, V> : ValidatorHolder<Flag, T, V>
{
    template <typename S, auto EF>
    CallbackHolder(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : ValidatorHolder<Flag, T, V>(t, i),
          applier(+[](Context& s, T& type) { Context::cast<S*>(s)->template runApplier<static_cast<typename S::type>(EF)>(false, type); }) {}
    ApplyFn<T> applier; ///< Callback invoked whenever the effective value changes.
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    static constexpr ApplyFn<T> applierImpl = H;
};

/**
 * @brief No-op base: primary template for when `H` is not an `OptApplyFn<T>` (no applier configured).
 * @ingroup CONFIG
 */
template <typename Flag, typename T, auto H, auto V> // applier, validator
struct OptionalCallbackHolder : Flag
{
    template <typename S, auto EF>
    OptionalCallbackHolder(std::in_place_type_t<S>, std::in_place_index_t<EF>) {};
};

/// @brief 'CallbackHolder' specialization that installs the optional callbacks.
template <typename Flag, typename T, auto H, auto V>
    requires std::is_same_v<decltype(H), OptApplyFn<T>>
struct OptionalCallbackHolder<Flag, T, H, V> : ValidatorHolder<Flag, T, V>
{
    template <typename S, auto EF>
    OptionalCallbackHolder(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : ValidatorHolder<Flag, T, V>(t, i),
          applier(+[](Context& s, T* type) { Context::cast<S*>(s)->template runApplier<static_cast<typename S::type>(EF)>(false, type); }) {}
    OptApplyFn<T> applier; ///< Callback invoked whenever the effective value changes.
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    static constexpr OptApplyFn<T> applierImpl = H; ///< Callback invoked whenever the effective value changes.
};

/**
 * @brief No-op base: primary template for when `H` is not a `ListApplyFn<T>` (no applier configured).
 * @ingroup CONFIG
 */
template <typename Flag, typename T, auto H, auto V> // applier, validator
struct ListCallbackHolder : Flag
{
    template <typename S, auto EF>
    ListCallbackHolder(std::in_place_type_t<S>, std::in_place_index_t<EF>) {};
};

/// @brief 'CallbackHolder' specialization that installs the list callbacks.
template <typename Flag, typename T, auto H, auto V>
    requires std::is_same_v<decltype(H), ListApplyFn<T>>
struct ListCallbackHolder<Flag, T, H, V> : ValidatorHolder<Flag, T, V>
{
    template <typename S, auto EF>
    ListCallbackHolder(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : ValidatorHolder<Flag, T, V>(t, i),
          applier(+[](Context& s, T& type, bool add) { Context::cast<S*>(s)->template runApplier<static_cast<typename S::type>(EF)>(false, type, add); }) {}
    ListApplyFn<T> applier; ///< Callback invoked whenever the effective value changes.
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    static constexpr ListApplyFn<T> applierImpl = H; ///< Callback invoked whenever the effective value changes.
};


/**
 * @brief No-op base: primary template for when `H` is not an `OwnedApplyFn<T, K>` (no applier configured).
 * @ingroup CONFIG
 */
template <typename Flag, typename T, typename K, auto H, auto V> // applier, validator
struct OwnedCallbackHolder : Flag
{
    template <typename S, auto EF>
    OwnedCallbackHolder(std::in_place_type_t<S>, std::in_place_index_t<EF>) {};
};

/// @brief 'CallbackHolder' specialization that installs the owned list callbacks.
template <typename Flag, typename T, typename K, auto H, auto V>
    requires (std::is_same_v<decltype(H), OwnedApplyFn<T, K>>)
struct OwnedCallbackHolder<Flag, T, K, H, V> : ValidatorHolder<Flag, K, V>
{
    template <typename S, auto EF>
    OwnedCallbackHolder(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : ValidatorHolder<Flag, K, V>(t, i),
          applier(+[](Context& s, T* type, const K& key) { Context::cast<S*>(s)->template runApplier<static_cast<typename S::type>(EF)>(false, type, key); }) {}
    OwnedApplyFn<T, K> applier = nullptr; ///< Callback invoked whenever the effective value changes.
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    static constexpr OwnedApplyFn<T, K> applierImpl = H; ///< Callback invoked whenever the effective value changes.
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
 * @brief Satisfied by any field type that exposes a static `validator` function pointer.
 *
 * When a field satisfies `RequiresValidation`, the owning @ref SubRegistry must
 * supply a @ref ContextProvider so that the validator can be called before the
 * field value changes.
 *
 * @tparam T  Field type to test.
 */
template <typename T>
concept RequiresValidation =
    requires { T::validator; };

/**
 * @brief Satisfied by field types that support masking (expose `setMask(const T*)`).
 *
 * @tparam T  Field type to test.
 */
template <typename T>
concept IsMaskable =
    requires(T& f, const T* p) { f.setMask(p); };

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
template <typename T, auto H = nullptr, auto V = nullptr>
class AtomicField : public CallbackHolder<AtomicFieldFlag, T, H, V>
{
public:
    template <typename S, auto EF>
    AtomicField(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : CallbackHolder<AtomicFieldFlag, T, H, V>(t, i)
    {}
    using type = T;
    void setMask(const AtomicField* p) noexcept { mask = p; }
    void clearMask() noexcept { mask = nullptr; }
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    template <IsAtomicField>
    friend class AtomicFieldAccessor;

    std::atomic<T> value{T{}};                          ///< Stored value; authoritative when state == CANNED.
    std::atomic<FieldState> state{FieldState::INHERIT}; ///< Whether a local override is active.
    uint32_t commandIndex = NO_COMMAND_INDEX;           ///< Node that last wrote the field.
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
template <typename T, auto H = nullptr, auto V = nullptr>
class OptionalAtomicField : public OptionalCallbackHolder<OptionalAtomicFieldFlag, T, H, V>
{
public:
    template <typename S, auto EF>
    OptionalAtomicField(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : OptionalCallbackHolder<OptionalAtomicFieldFlag, T, H, V>(t, i)
    {}
    using type = T;
    void setMask(const OptionalAtomicField* p) noexcept { mask = p; }
    void clearMask() noexcept { mask = nullptr; }
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    template <IsOptionalAtomicField>
    friend class OptionalAtomicFieldAccessor;

    std::atomic<T> value{};                             ///< Stored value; authoritative when state == CANNED.
    std::atomic<FieldState> state{FieldState::INHERIT}; ///< Whether a local value has been set.
    uint32_t commandIndex = NO_COMMAND_INDEX;           ///< Node that last wrote the field.
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
template <typename T, auto H = nullptr, auto V = nullptr>
class ValueField : public OptionalCallbackHolder<ValueFieldFlag, T, H, V>
{
public:
    template <typename S, auto EF>
    ValueField(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : OptionalCallbackHolder<ValueFieldFlag, T, H, V>(t, i)
    {}
    using type = T;
    void setMask(const ValueField* p) noexcept { mask = p; }
    void clearMask() noexcept { mask = nullptr; }

    ~ValueField() { delete value.load(std::memory_order_relaxed); }
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    template <IsValueField>
    friend class ValueFieldAccessor;

    std::atomic<type*> value = nullptr;                  ///< Guarded value; authoritative when state == CANNED.
    std::atomic<FieldState> state{FieldState::INHERIT};  ///< Whether a local value has been set.
    uint32_t commandIndex = NO_COMMAND_INDEX;            ///< Node that last wrote the field.
    const ValueField* mask = nullptr;
};

/**
 * @brief Mutex-protected config field for non-atomic value types.
 * @ingroup CONFIG
 *
 * Use `ListField<T>` when `T` cannot be held in a `std::atomic` — for
 * example, `std::string`, `std::vector`, or other heap-allocated types.
 * Reads and writes go through @ref ListFieldAccessor's `readEach()` /
 * `get()` / `add()` / `erase()`, which hold the owning registry's mutex for
 * the duration of the call.
 *
 * Unlike @ref AtomicField, `ListField` does not participate in parent
 * inheritance: it has no `state` or `mask` member. The list is allocated
 * unconditionally by the constructor, so the value pointer is never null.
 * It does support the optional `ApplyFn` callback.
 *
 * ## Concurrency Model
 * - The mutex lives in the owning `SubRegistry` and is handed to
 *   @ref ListFieldAccessor on construction; the field itself stores only the
 *   value pointer. All `ListField` and `ValueField` members of the same
 *   registry share that one mutex.
 * - Each accessor method takes a `std::lock_guard` on it for the duration of
 *   the call. Do not call one from inside another.
 * - The destructor deletes the list without locking; see the ownership note below.
 *
 * @tparam T  Value type (heap-allocated or non-atomic-capable).
 * @tparam H  Optional applier callback; see @ref AtomicField for details.
 *
 * @see AtomicField
 * @see SubRegistry
 */
template <typename T, auto H = nullptr, auto V = nullptr>
class ListField : public ListCallbackHolder<ListFieldFlag, T, H, V>
{
public:
    template <typename S, auto EF>
    ListField(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : ListCallbackHolder<ListFieldFlag, T, H, V>(t, i),
          value(new type())
    {}
    using element = T;
    using type    = std::vector<element>;

    ~ListField() { delete value.load(std::memory_order_relaxed); }
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    template <IsListField>
    friend class ListFieldAccessor;

    std::atomic<type*> value = nullptr; ///< Guarded value.
    uint32_t commandIndex = NO_COMMAND_INDEX;
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
template <typename T, typename K, auto H = nullptr, auto V = nullptr>
class OwnedListField : public OwnedCallbackHolder<OwnedListFieldFlag, T, K, H, V>
{
public:
    template <typename S, auto EF>
    OwnedListField(std::in_place_type_t<S> t, std::in_place_index_t<EF> i)
        : OwnedCallbackHolder<OwnedListFieldFlag, T, K, H, V>(t, i)
    {}
    using type = T; ///< Child entry type.
    using key = K;  ///< Key type used to look up children.

    ~OwnedListField()
    {
        for (auto& [k, v] : children)
            if (v) delete v;
    }
    OwnedListField(const OwnedListField&) = delete;
    OwnedListField& operator=(const OwnedListField&) = delete;
private:
    template <typename, typename, typename>
    friend class SubRegistry;
    template <IsOwnedListField>
    friend class OwnedListFieldAccessor;

    std::unordered_map<key, type*> children{}; ///< Pointer-owning child entries.
    uint32_t commandIndex = NO_COMMAND_INDEX; ///< Node that last wrote the field.
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

    // Comparison is a deliberate no-op: this field is excluded from equality by design.
    friend bool operator==(const IgnoreCompare&, const IgnoreCompare&) { return true; }
    friend bool operator!=(const IgnoreCompare&, const IgnoreCompare&) { return false; }
};
}

#endif // REGISTRY_TYPES_HPP

