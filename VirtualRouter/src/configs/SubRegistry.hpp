/**
 * @file SubRegistry.hpp
 * @brief Hierarchical configuration registry: field storage, inheritance, and masking.
 *
 * Implements a template-based configuration container that manages typed fields
 * organized by an enum index. Supports inheritance hierarchy with field masking
 * (child registries inherit parent values by default). Uses compile-time type
 * checking and optional field values for flexible configuration schemas.
 *
 * ## Features
 * - **Field storage**: Type-safe tuple-based storage for heterogeneous fields
 * - **Inheritance**: Child SubRegistry instances inherit parent field values
 * - **Masking**: Child fields can override parent defaults via optional wrapper
 * - **Context support**: Optional ContextProvider for fields requiring external data
 * - **Thread-safe**: Mutex-guarded access for concurrent configuration updates
 *
 * ## Usage
 * ```cpp
 * enum class MyConfig { TIMEOUT, RETRIES, COUNT };
 * using MyRegistry = SubRegistry<MyConfig,
 *     AtomicField<uint16_t CONFIG_INDEX_ARG(MyConfig::TIMEOUT)>,
 *     AtomicField<uint8_t CONFIG_INDEX_ARG(MyConfig::RETRIES)>
 * >;
 * ```
 */

#ifndef SUB_REGISTRY_HPP
#define SUB_REGISTRY_HPP

#include <tuple>
#include <utility>
#include <cstddef>
#include <mutex>
#include <typeinfo>
#include <stdexcept>
#include <RCU.hpp>

#include "RegistryTypes.hpp"
#include "RegistryDefaultTable.hpp"

namespace config
{
enum class Global;

/**
 * @brief Convert enum value to zero-based index for array/tuple access.
 *
 * @tparam E Enum constant
 * @return Constexpr size_t equal to static_cast<size_t>(E)
 */
template <auto E>
inline constexpr size_t toIndex = static_cast<size_t>(E);

/**
 * @brief Empty base tag that identifies a type as a `SubRegistry` specialization.
 *
 * All `SubRegistry<ENUM, Fields...>` instances inherit from this struct so that
 * the `IsSubRegistry` concept can detect them without depending on the full
 * template parameter list.
 */
struct SubRegistryFlag {};

template <typename T>
concept IsSubRegistry = 
    std::derived_from<T, SubRegistryFlag>;

template <typename T>
concept IsSubRegistryWrapper =
    requires(T& object) {
        []<typename ENUM, ApplyFn H, typename... Fields>
        (SubRegistry<T, ENUM, H, Fields...>&) {}(object);
    };

template <typename T>
concept IsFieldTuple =
    requires(T* obj) { []<typename... Fields>(FieldTuple<Fields...>*){}(obj); };

/**
 * TODO finish doxy
 */
template <typename ...Field>
struct FieldTuple
{
    using Type = std::tuple<Field...>;
};

/**
 * @class SubRegistry
 * @brief Hierarchical configuration registry with field masking and inheritance.
 *
 * Template-based registry that stores typed configuration fields indexed by an enum.
 * Supports parent-child relationships where children inherit parent field values
 * via masking. All fields are stored in a tuple and accessed by enum constant.
 *
 * ## Architecture
 * - **Field tuple**: Each enum value corresponds to one field in the tuple
 * - **Inheritance**: Child registries can inherit from parent, with fields masked
 * - **Optional storage**: Fields are stored in `optional<Field>` for lazy initialization
 * - **Thread-safe**: Mutex protects concurrent field access/modification
 *
 * ## Type requirements
 * - `ENUM`: Enum class with `COUNT` as last value (field count)
 * - `Fields...`: Variadic field types (AtomicField, OptionalAtomicField, etc.)
 *
 * ## Compile-time checks
 * - Number of Fields must equal ENUM::COUNT
 * - Field indices must match their tuple positions
 * - Each AtomicField must have a default value in RegistryDefaultTable
 *
 * @tparam ENUM Enum type for field indexing
 * @tparam Fields Tuple of field types (one per enum value)
 *
 * @see RegistryTypes.hpp, RegistryDefaultTable.hpp
 */
template <typename Base, typename ENUM, ApplyFn H, typename Fields>
class SubRegistry : public SubRegistryFlag
{
public:
    static_assert(IsFieldTuple<Fields>, "Must be a FieldTuple type.");

    using sub     = SubRegistry;
    using type    = ENUM;
    static constexpr ApplyFn applier = H; ///< Callback invoked whenever the effective value changes.

    // Meta tuple: used ONLY for compile-time checks / type indexing.
    /// Type alias for the field tuple (for type checking only, not storage).
    using FieldTuple = Fields::Type;

    template <ENUM i>
    using FieldTypeAt = std::tuple_element_t<static_cast<size_t>(i), FieldTuple>;

    static_assert(std::tuple_size_v<FieldTuple> == config::toIndex<ENUM::COUNT>);

#if USE_CONFIG_INDEX
    static_assert(
        []<size_t... Is>(std::index_sequence<Is...>) constexpr
        {
            return (
                (static_cast<size_t>(
                    std::tuple_element_t<Is, FieldTuple>::field
                ) == Is) && ...
            );
        }(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{}),
        "Tuple order must match enum field indicies"
    );
#endif
    SubRegistry()
        : ctxProvider(),
          fields(),
          parent(this),
          parentType(typeid(*this))
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    ~SubRegistry()
    {
        removeMask();
        for (SubRegistry* sub : derived)
            sub->removeMask();
    }

    template <ENUM F>
    FieldTypeAt<F>::type getDefault() const noexcept
    requires IsAtomicField<FieldTypeAt<F>>
    {
        return Entry<ENUM, F>::template get<FieldTypeAt<F>::type>();
    }

    /**
     * @brief Gets this registry's own context provider (call `.set(this)` from the owner).
     *
     * Every registry instance owns an independent `ContextProvider`; it is
     * @b not shared with or inherited from a masked parent/base registry
     * (the masking constructor only copies it once, at construction, as a
     * convenience default) nor from an enclosing registry when this instance
     * lives inside a `RegistryContainer` field. Whatever object logically
     * owns this specific registry must call `context().set(this)` on it
     * directly for its fields' appliers to fire. See the file-level comment
     * above for the full masking-vs-context distinction.
     *
     * @return Reference to this registry's own context provider.
     */
    ContextProvider& context() const noexcept { return ctxProvider; }

    /**
     * @brief Returns a typed accessor for the field identified by enum constant `F`.
     *
     * The returned accessor type depends on the underlying field kind:
     * - `AtomicField`         → `AtomicFieldAccessor<F>`
     * - `OptionalAtomicField` → `OptionalAtomicFieldAccessor<F>`
     * - `ValueField`          → `ValueFieldAccessor<F>`
     * - `ListField`           → `ListFieldAccessor<F>`
     * - `OwnedListField`      → `OwnedListFieldAccessor<F>`
     * - `RegistryContainer`   → direct reference to the contained value
     *
     * Accessors carry the context provider and applier pointer so that `set()`/
     * `unset()` fire the live-notification callback automatically.
     *
     * @tparam F Enum constant identifying the field.
     * @return An accessor object or reference for the requested field.
     */
    template <ENUM F>
    decltype(auto) get() noexcept;

    /**
     * @brief Const overload of @ref get(); returns a const accessor.
     *
     * @tparam F Enum constant identifying the field.
     * @return A const accessor object or reference for the requested field.
     */
    template <ENUM F>
    decltype(auto) get() const noexcept;

    /**
     * @brief Gets the mask-time parent's copy of field `F` (mutable).
     *
     * @warning `base` reflects only the mask passed to the *constructor*.
     * `setMask()` (called post-construction to re-point or clear masking,
     * e.g. `NeighborConfigs::setPeerGroup`) updates each field's own `mask`
     * pointer via `applyMask()` but does @b not update `base`. If `setMask()`
     * has been called since construction, `base` may be stale/out of sync
     * with what the fields actually mask against -- currently unused
     * elsewhere in the codebase, so this has not surfaced as a live bug, but
     * do not rely on it after a `setMask()` call without first reconciling
     * the two.
     *
     * @tparam F Enum constant identifying the field
     * @return Const reference to the field value
     */
    template <ENUM F>
    FieldTypeAt<F>* getInherited() noexcept
    {
        if (inherited)
            return &inherited->getValue<F>();
        return nullptr;
    }

    /**
     * @brief Gets the mask-time parent's copy of field `F` (const).
     *
     * @warning See the mutable overload above -- `base` can go stale relative
     * to the per-field `mask` pointers after a post-construction `setMask()`.
     *
     * @tparam F Enum constant identifying the field
     * @return Const reference to the field value
     */
    template <ENUM F>
    const FieldTypeAt<F>* getInherited() const noexcept
    {
        if (inherited)
            return &inherited->getValue<F>();
        return nullptr;
    }

    /**
     * @brief Gets a field by enum constant (mutable).
     *
     * @tparam F Enum constant identifying the field
     * @return Reference to the field value
     */
    template <ENUM F>
    decltype(auto) getValue() noexcept
    {
        constexpr size_t I = config::toIndex<F>;
        return (std::get<I>(fields));
    }

    /**
     * @brief Gets a field by enum constant (const).
     *
     * @tparam F Enum constant identifying the field
     * @return Const reference to the field value
     */
    template <ENUM F>
    const FieldTypeAt<F>& getValue() const noexcept
    {
        constexpr size_t I = config::toIndex<F>;
        return (std::get<I>(fields));
    }

    /**
     * @brief Checks whether this registry was constructed with a mask parent.
     *
     * @warning Reflects `base`, which is set only by the constructors and is
     * @b not updated by `setMask()`. See the `@warning` on `getInherited()`.
     *
     * @return True if this registry was constructed via the masking
     * constructor (`SubRegistry(P&, SubRegistry&)`).
     */
    bool isMasked() const noexcept
    {
        return inherited != nullptr;
    }

    /**
     * @brief (Re-)masks every maskable field against `mask`, or clears masking.
     *
     * For each field whose type exposes `setMask(const Field*)`, points that
     * field's `mask` pointer at the corresponding field in `mask` (or clears
     * it to `nullptr` when `mask == nullptr`, e.g. detaching a neighbor from
     * its peer group). Subsequent `load()` calls on an `INHERIT`-state field
     * walk this new pointer. Locally-overridden (`CANNED`) field values are
     * untouched -- masking only affects fields currently deferring to a
     * parent, it never resets an existing override back to inherited.
     *
     * @note This only rewires per-field value inheritance. It does @b not
     * touch `ctxProvider` (this registry keeps notifying whatever owner
     * already called `context().set(...)` on it, regardless of which
     * template it now masks against) and does @b not update `base` (see the
     * `@warning` on `isMasked()`/`getInherited()`).
     *
     * @param mask Pointer to the new mask/parent SubRegistry, or `nullptr` to
     * clear masking (all `INHERIT` fields fall back to their own defaults).
     */
    void setMask(SubRegistry* mask)
    {
        if (inherited && inherited != mask)
            inherited->unregisterDerived(this);
        applyMask(mask, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        if (mask && mask != inherited)
            mask->registerDerived(this);
        inherited = mask;
    }

    /**
     * TODO add doxy comment
     */
    void removeMask()
    {
        if (!inherited)
            return;
        removeMask(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        inherited->unregisterDerived(this);
        inherited = nullptr;
    }

    template <typename P>
    P& resolveParent()
    {
        if (parentType != typeid(P))
            throw std::runtime_error("Parent type mismatch");
        return *static_cast<P*>(parent);
    }

    template <ENUM EF>
    void runApplier(bool derivedCall = false)
    {
        using F = FieldTypeAt<EF>;
        if (ctxProvider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(ctxProvider.get());
            if (applier)
                applier(ctxProvider.get());
        }
        for (SubRegistry* ctx : derived)
        {
            if constexpr (IsMaskable<F>)
            {
                F& derivedField = ctx->template getValue<EF>();
                if (derivedCall && derivedField.state.load(std::memory_order_relaxed) != FieldState::INHERIT)
                    continue;
            }
            ctx->template runApplier<EF>(true);
        }
    }

    mutable std::mutex mu; ///< Synchronization mutex for concurrent field access.

    /**
     * @brief Automatically initialises every unbound @ref RegistryContainer field.
     *
     * Called by @ref RegistryDatabase immediately after a new slot is constructed
     * so that each `RegistryContainer<U>` receives its own registry slot without
     * requiring a separate `emplace()` call at the use site.
     *
     * @note The nested `U` created here is a brand-new, independent
     * `SubRegistry` with its own blank `ctxProvider` -- it is not related to
     * this (enclosing) registry's masking or context in any way. If fields
     * inside that nested registry need live notification, its owner must
     * call `.context().set(owner)` on it directly; nothing here does that
     * automatically.
     *
     * @tparam Entries  Pack of all entry types registered in the database.
     * @param  db       The owning database used to allocate child slots.
     */
    template <typename... Entries>
    void initContainers(RegistryDatabase<Entries...>& db)
    {
        initContainersImpl(db, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    /**
     * @brief Returns the context provider for this registry.
     *
     * Accessor constructors call this to obtain the provider reference so that
     * `set()`/`unset()` can fire the applier callback when a live context is
     * registered. Exposed as public so that the accessor implementation in
     * FieldAccessor.hpp can reach it without friendship.
     *
     * @return Reference to the `ContextProvider` owned by this registry.
     */
    ContextProvider& getProvider()
    {
        return ctxProvider;
    }

private:
    template <typename T, typename K CONFIG_INDEX_PARAM, auto A>
    friend class OwnedListField;

    /**
     * @brief Constructs a root registry with no parent.
     *
     * Initializes all fields with default values from RegistryDefaultTable.
     * 
     * @param parent Pointer to parent SubRegistry.
     */
    template <typename P>
    explicit SubRegistry(P& p) noexcept
        : ctxProvider(),
          fields(),
          parent(&p),
          parentType(typeid(p))
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    /**
     * @brief Constructs a child registry that inherits from a parent.
     *
     * All fields are initialized and then masked with parent field values.
     * Child can override parent values on a per-field basis.
     *
     * @note `ctxProvider(mask.ctxProvider)` copies the mask's context pointer
     * as a one-time construction-time default only -- it is not a live link.
     * In practice this copy is almost always immediately overwritten: the
     * real owner of this new registry (e.g. a `Neighbor`) is expected to call
     * `context().set(this)` right after construction, which is what actually
     * makes appliers fire against the correct owner. See the file-level
     * comment for why masking and context must not be conflated.
     *
     * @param parent Pointer to parent SubRegistry.
     * @param mask Reference to inherited SubRegistry of the same type.
     */
    template <typename P>
    SubRegistry(P& p, SubRegistry& mask)
        : ctxProvider(mask.ctxProvider),
          fields(),
          parent(&p),
          parentType(typeid(p))
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        applyMask(inherited, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    template <typename... Entries, size_t... I>
    void initContainersImpl(RegistryDatabase<Entries...>& db, std::index_sequence<I...>)
    {
        ([&]<size_t Index>() {
            using F = std::tuple_element_t<Index, FieldTuple>;
            if constexpr (IsRefContainer<F>)
                db.autoInit(std::get<Index>(fields));
        }.template operator()<I>(), ...);
    }

    /// Creates constructor arguments for a field (context/mutex/parent as needed).
    template <typename F>
    auto createField()
    {
        return std::tuple<>{};
    }

    /// Creates constructor arguments for a masked field (includes parent field).
    template <typename F>
    auto createMaskedField(const F& parentField)
    {
        if constexpr (IsListField<F> || IsValueField<F>)
        {
            if constexpr (RequiresContext<F>)
                return std::forward_as_tuple(ctxProvider, mu, parentField);
            else
                return std::forward_as_tuple(mu, parentField);
        }
        else if constexpr (RequiresContext<F>)
            return std::forward_as_tuple(ctxProvider, parentField);
        else
            return std::forward_as_tuple(parentField);
    }

    /**
     * Constructs all fields (fold expression over index_sequence).
     * The `fields()` member initializer has already default-constructed every
     * field, so each one must be destroyed before construct_at reconstructs it
     * in place -- otherwise fields that allocate (RegistryContainer) orphan
     * their first allocation and leak one payload per field, per registry.
     */
    template <size_t... I>
    void constructFields(std::index_sequence<I...>) noexcept
    {
        ([&]<size_t Index>() {
            using F = std::tuple_element<Index, FieldTuple>;
            std::destroy_at(&std::get<Index>(fields));
            std::apply(
                [&](auto&&... args) {
                    std::construct_at(&std::get<Index>(fields), std::forward<decltype(args)>(args)...);
                },
                this->template createField<F>()
            );
        }.template operator()<I>(), ...);
    }

    /// Installs default values from RegistryDefaultTable for all AtomicFields.
    template <size_t... I>
    void installDefaults(std::index_sequence<I...>) noexcept
    {
        ([&]<size_t Index>() {
            using Field = std::tuple_element_t<Index, FieldTuple>;
            if constexpr (config::IsAtomicField<Field>)
            {
                constexpr ENUM E = static_cast<ENUM>(Index);
                auto f = get<E>();
                if constexpr (hasV<ENUM, E>)
                    f.setDefault();
            }
        }.template operator()<I>(), ...);
    }

    /**
     * @brief Points each maskable field's `mask` pointer at `mask`'s field, or clears it.
     *
     * Only touches fields whose type exposes `setMask(const Field*)` (atomic /
     * optional-atomic / value fields); other field kinds are left alone.
     * `mask == nullptr` clears every such field's `mask` pointer to `nullptr`
     * (the null check here must be on `mask`, the parameter -- not on this
     * object's own `parent`, which is set at construction and effectively
     * always non-null, so it can never signal "clear the mask").
     */
    template <size_t... I>
    void applyMask(SubRegistry* mask, std::index_sequence<I...>) noexcept
    {
        ([&]<size_t Index>() {
            using Field = std::tuple_element_t<Index, FieldTuple>;
            auto& local = std::get<Index>(fields);
            if constexpr (IsMaskable<Field>)
            {
                const Field* parentField =
                    mask ? &std::get<Index>(mask->fields) : nullptr;
                local.setMask(parentField);
            }
        }.template operator()<I>(), ...);
    }

    /**
     * TODO add doxy comment
     */
    template <size_t... I>
    void removeMask(std::index_sequence<I...>) noexcept
    {
        ([&]<size_t Index>() {
            using Field = std::tuple_element_t<Index, FieldTuple>;
            auto& local = std::get<Index>(fields);
            if constexpr (requires(Field& f) { f.clearMask(); })
            {
                local.clearMask();
            }
        }.template operator()<I>(), ...);
    }

    /**
     * TODO add doxy comment
     */
    void registerDerived(SubRegistry* ctx)
    {
        derived.push_back(ctx);
    }

    /**
     * TODO add doxy comment
     */
    void unregisterDerived(SubRegistry* ctx)
    {
        derived.erase(std::find(
            derived.begin(),
            derived.end(),
            ctx
        ));
    }

    /// Context provider for fields requiring external data.
    mutable ContextProvider ctxProvider;
    
    /// Optional storage for all fields (allows lazy initialization).
    FieldTuple fields;

    /// Parent pointer, must point to the parent object
    void* const parent = nullptr;
    const std::type_info& parentType;
    
    /// Pointer to parent registry for field inheritance (nullptr if root).
    SubRegistry* inherited{nullptr};
    std::vector<SubRegistry*> derived;
};
}

#endif // SUB_REGISTRY_HPP
