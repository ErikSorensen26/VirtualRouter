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
          parentType(typeid(*this)),
          base(nullptr)
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    template <ENUM F>
    FieldTypeAt<F>::type getDefault() const noexcept
    requires IsAtomicField<FieldTypeAt<F>>
    {
        return Entry<ENUM, F>::template get<FieldTypeAt<F>::type>();
    }

    /**
     * @brief Gets the context provider for fields that require external data.
     *
     * @return Reference to the context provider shared with parent (if masked).
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
     * @brief Gets a inherited field by enum constant (mutable).
     *
     * @tparam F Enum constant identifying the field
     * @return Const reference to the field value
     */
    template <ENUM F>
    FieldTypeAt<F>* getInherited() noexcept
    {
        if (base)
            return &base->getValue<F>();
        return nullptr;
    }

    /**
     * @brief Gets a inherited field by enum constant (const).
     *
     * @tparam F Enum constant identifying the field
     * @return Const reference to the field value
     */
    template <ENUM F>
    const FieldTypeAt<F>* getInherited() const noexcept
    {
        if (base)
            return &base->getValue<F>();
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
     * @brief Checks whether this registry is masked by a parent.
     *
     * @return True if this registry has a parent and inherits its values.
     */
    bool isMasked() const noexcept
    {
        return base != nullptr;
    }

    /**
     * @brief Applies masking by a new parent (resets field inheritance).
     *
     * Used after construction to change or establish parent relationship.
     * All maskable fields are reset to inherit from the new parent.
     *
     * @param parent Pointer to parent SubRegistry (or nullptr to remove masking).
     */
    void setMask(SubRegistry* mask)
    {
        applyMask(mask, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    template <typename P>
    P& resolveParent()
    {
        if (parentType != typeid(P))
            throw std::runtime_error("Parent type mismatch");
        return *static_cast<P*>(parent);
    }

    mutable std::mutex mu; ///< Synchronization mutex for concurrent field access.

    /**
     * @brief Automatically initialises every unbound @ref RegistryContainer field.
     *
     * Called by @ref RegistryDatabase immediately after a new slot is constructed
     * so that each `RegistryContainer<U>` receives its own registry slot without
     * requiring a separate `emplace()` call at the use site.
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
          parentType(typeid(p)),
          base(nullptr)
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
     * @param parent Pointer to parent SubRegistry.
     * @param mask Reference to inherited SubRegistry of the same type.
     */
    template <typename P>
    SubRegistry(P& p, SubRegistry& mask)
        : ctxProvider(mask.ctxProvider),
          fields(),
          parent(&p),
          parentType(typeid(p)),
          base(&mask)
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        applyMask(base, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
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

    /// Constructs all fields (fold expression over index_sequence).
    /// The `fields()` member initializer has already default-constructed every
    /// field, so each one must be destroyed before construct_at reconstructs it
    /// in place -- otherwise fields that allocate (RegistryContainer) orphan
    /// their first allocation and leak one payload per field, per registry.
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

    /// Applies masking to all fields from a parent registry.
    template <size_t... I>
    void applyMask(SubRegistry* mask, std::index_sequence<I...>) noexcept
    {
        ([&]<size_t Index>() {
            using Field = std::tuple_element_t<Index, FieldTuple>;
            auto& local = std::get<Index>(fields);
            if constexpr (requires(Field& f, const Field* p) { f.setMask(p); })
            {
                const Field* parentField =
                    parent ? &std::get<Index>(mask->fields) : nullptr;
                local.setMask(parentField);
            }
        }.template operator()<I>(), ...);
    }

    /// Context provider for fields requiring external data.
    mutable ContextProvider ctxProvider;
    
    /// Optional storage for all fields (allows lazy initialization).
    FieldTuple fields;

    /// Parent pointer, must point to the parent object
    void* const parent = nullptr;
    const std::type_info& parentType;
    
    /// Pointer to parent registry for field inheritance (nullptr if root).
    SubRegistry* base{nullptr};
};
}

#endif // SUB_REGISTRY_HPP
