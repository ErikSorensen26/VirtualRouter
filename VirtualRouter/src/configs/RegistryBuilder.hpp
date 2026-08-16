/**
 * @file RegistryBuilder.hpp
 * @brief The macros a registry is written in, and everything generated from one.
 * @ingroup CONFIG
 *
 * A registry is declared as a single field list -- one line per field, naming
 * its kind, type and default -- and DEFINE_CONFIG_GROUP expands that one list
 * into all the parallel things that have to agree about it: the field enum, the
 * name-hash table the grammar resolves against, the per-field default
 * specializations, the storage tuple, and the SubRegistry that owns them.
 *
 * Deriving them from one list is the point. The enum's order is the tuple's
 * order is the hash table's order, and a field added in the middle moves all
 * three together, so there is no way for them to drift apart.
 *
 * @see RegistryTable.hpp, which gathers the registries into the id list.
 */

#ifndef REGISTRY_BUILDER_HPP
#define REGISTRY_BUILDER_HPP

#include <type_traits> // IWYU pragma: keep
#include <string_view> // IWYU pragma: keep
#include <cstdint> // IWYU pragma: keep

#include "configs/TupleSchema.hpp" // IWYU pragma: keep
#include "configs/RegistryTraits.hpp" // IWYU pragma: keep
#include "configs/RegistryDefaultTable.hpp" // IWYU pragma: keep
#include "configs/SubRegistry.hpp" // IWYU pragma: keep
#include "configs/RegistryReference.hpp" // IWYU pragma: keep
#include "configs/FieldAccessor.hpp" // IWYU pragma: keep

#define TODO /*TODO*/

#define CONFIG_CAT_(a, b) a##b
#define CONFIG_CAT(a, b) CONFIG_CAT_(a, b)

#define CONFIG_TAIL(first, ...) __VA_ARGS__
#define CONFIG_STRIP_LEADING_COMMA(...) CONFIG_TAIL(__VA_ARGS__)

// OwnedListField
#define OWNED_LIST_FIELD(X, E, NAME, TYPE, KEY)                     X(E, OwnedListField, NAME, TYPE, 0, ~, 0, 3, KEY)
#define OWNED_LIST_FIELD_CB(X, E, NAME, TYPE, KEY)                  X(E, OwnedListField, NAME, TYPE, 0, ~, 4, 4, KEY)
#define OWNED_LIST_FIELD_CB_VA(X, E, NAME, TYPE, KEY)               X(E, OwnedListField, NAME, TYPE, 0, ~, 8, 5, KEY)

// AtomicField
#define ATOMIC_FIELD(X, E, NAME, TYPE, DEF)                         X(E, AtomicField, NAME, TYPE, 1, DEF, 0, 0, ~)
#define ATOMIC_FIELD_CB(X, E, NAME, TYPE, DEF)                      X(E, AtomicField, NAME, TYPE, 1, DEF, 2, 1, ~)
#define ATOMIC_FIELD_CB_VA(X, E, NAME, TYPE, DEF)                   X(E, AtomicField, NAME, TYPE, 1, DEF, 6, 1, ~)

// OptionalAtomicField
#define OPTIONAL_ATOMIC_FIELD(X, E, NAME, TYPE)                     X(E, OptionalAtomicField, NAME, TYPE, 0, ~, 0, 0, ~)
#define OPTIONAL_ATOMIC_FIELD_CB(X, E, NAME, TYPE)                  X(E, OptionalAtomicField, NAME, TYPE, 0, ~, 1, 1, ~)
#define OPTIONAL_ATOMIC_FIELD_CB_VA(X, E, NAME, TYPE)               X(E, OptionalAtomicField, NAME, TYPE, 0, ~, 5, 1, ~)

// ListField
#define LIST_FIELD(X, E, NAME, TYPE)                                X(E, ListField, NAME, TYPE, 0, ~, 0, 0, ~)
#define LIST_FIELD_CB(X, E, NAME, TYPE)                             X(E, ListField, NAME, TYPE, 0, ~, 3, 1, ~)
#define LIST_FIELD_CB_VA(X, E, NAME, TYPE)                          X(E, ListField, NAME, TYPE, 0, ~, 7, 1, ~)

// ValueField
#define VALUE_FIELD(X, E, NAME, TYPE)                               X(E, ValueField, NAME, TYPE, 0, ~, 0, 0, ~)
#define VALUE_FIELD_CB(X, E, NAME, TYPE)                            X(E, ValueField, NAME, TYPE, 0, ~, 1, 1, ~)
#define VALUE_FIELD_CB_VA(X, E, NAME, TYPE)                         X(E, ValueField, NAME, TYPE, 0, ~, 5, 1, ~)

// RegistryContainer
#define REGISTRY_CONTAINER(X, E, NAME, TYPE)                        X(E, RegistryContainer, NAME, TYPE, 0, ~, 0, 0, ~)

// OptionalRegistryContainer
#define OPTIONAL_REGISTRY_CONTAINER(X, E, NAME, TYPE)               X(E, OptionalRegistryContainer, NAME, TYPE, 0, ~, 0, 0, ~)
#define OPTIONAL_REGISTRY_CONTAINER_CB(X, E, NAME, TYPE)            X(E, OptionalRegistryContainer, NAME, TYPE, 0, ~, 1, 1, ~)
#define OPTIONAL_REGISTRY_CONTAINER_CB_VA(X, E, NAME, TYPE)         X(E, OptionalRegistryContainer, NAME, TYPE, 0, ~, 5, 1, ~)

#define CONFIG_ENUM_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, CBV, SPEC, KEY) NAME,
#define CONFIG_HASH_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, CBV, SPEC, KEY) tokenHash(#NAME),

#define CONFIG_DEF_ENTRY_SPEC_1(E, NAME, DEF) \
    template <> \
    struct Entry<E, E::NAME> \
    { \
        static constexpr bool has = true; \
        template <typename T> \
        static constexpr T get() noexcept { return static_cast<T>(DEF); } \
    };
#define CONFIG_DEF_ENTRY_SPEC_0(E, NAME, DEF)

#define CONFIG_DEF_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, CBV, SPEC, KEY) \
    CONFIG_CAT(CONFIG_DEF_ENTRY_SPEC_, HASDEF)(E, NAME, DEF)

#define CONFIG_CBV_SPEC_1(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE*);
#define CONFIG_CBV_SPEC_2(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE&);
#define CONFIG_CBV_SPEC_3(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE&, bool);
#define CONFIG_CBV_SPEC_4(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE*, const KEY&);
#define CONFIG_CBV_SPEC_5(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE*); \
    bool NAME##_VA(config::Context&, TYPE&);
#define CONFIG_CBV_SPEC_6(NAME, TYPE, CB, VA, KEY) \
    void NAME##_CB(config::Context&, TYPE&); \
    bool NAME##_VA(config::Context&, TYPE&);
#define CONFIG_CBV_SPEC_7(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE&, bool); \
    bool NAME##_VA(config::Context&, TYPE&);
#define CONFIG_CBV_SPEC_8(NAME, TYPE, KEY) \
    void NAME##_CB(config::Context&, TYPE*, const KEY&); \
    bool NAME##_VA(config::Context&, KEY&);
#define CONFIG_CBV_SPEC_0(NAME, TYPE, KEY)

#define CONFIG_CBH_VALID_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, CBV, SPEC, KEY) \
    CONFIG_CAT(CONFIG_CBV_SPEC_, CBV)(NAME, TYPE, KEY) \

#define CONFIG_FIELD_SPEC_0(KIND, TYPE, E, NAME, KEY) KIND<TYPE>
#define CONFIG_FIELD_SPEC_1(KIND, TYPE, E, NAME, KEY) KIND<TYPE, E##Handlers::NAME##_CB>
#define CONFIG_FIELD_SPEC_2(KIND, TYPE, E, NAME, KEY) KIND<TYPE, E##Handlers::NAME##_CB, E##Handlers::NAME##_VA>
#define CONFIG_FIELD_SPEC_3(KIND, TYPE, E, NAME, KEY) KIND<TYPE, KEY>
#define CONFIG_FIELD_SPEC_4(KIND, TYPE, E, NAME, KEY) KIND<TYPE, KEY, E##Handlers::NAME##_CB>
#define CONFIG_FIELD_SPEC_5(KIND, TYPE, E, NAME, KEY) KIND<TYPE, KEY, E##Handlers::NAME##_CB, E##Handlers::NAME##_VA>

#define CONFIG_FIELDTUPLE_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, CBV, SPEC, KEY) \
    , CONFIG_CAT(CONFIG_FIELD_SPEC_, SPEC)(KIND, TYPE, E, NAME, KEY)

#define DEFINE_CONFIG_GROUP(NAME, LIST) \
    namespace NAME##Handlers { LIST(CONFIG_CBH_VALID_ENTRY, NAME) } \
    enum class NAME { LIST(CONFIG_ENUM_ENTRY, NAME) COUNT }; \
    static constexpr uint32_t NAME##Hashes[] = \
    { LIST(CONFIG_HASH_ENTRY, NAME) tokenHash("COUNT") }; \
    template <> \
    struct HashRegistry<NAME> \
    { static constexpr decltype(NAME##Hashes)& hashes = NAME##Hashes; }; \
    LIST(CONFIG_DEF_ENTRY, NAME) \
    struct NAME##Fields : FieldTuple< \
        CONFIG_STRIP_LEADING_COMMA(LIST(CONFIG_FIELDTUPLE_ENTRY, NAME)) \
    > {}; \
    struct NAME##Registry : public SubRegistry<NAME##Registry, NAME, NAME##Fields> \
    { using SubRegistry<NAME##Registry, NAME, NAME##Fields>::SubRegistry; }; \
    template <> \
    struct RegistryOf<NAME> { using type = NAME##Registry; };

// FUNCTION CREATION

template <typename T>
struct FnTraits;

template <typename T>
struct FnTraits<void(*)(config::Context&, T&)>
    { using Ret = void; using T1 = T&; using T2 = void; };
template <typename T>
struct FnTraits<void(*)(config::Context&, T*)>
    { using Ret = void; using T1 = T*; using T2 = void; };
template <typename T>
struct FnTraits<void(*)(config::Context&, T&, bool)>
    { using Ret = void; using T1 = T&; using T2 = bool; };
template <typename T, typename K>
struct FnTraits<void(*)(config::Context&, T*, const K&)>
    { using Ret = void; using T1 = T*; using T2 = const K&; };
template <typename T>
struct FnTraits<bool(*)(config::Context&, T&)>
    { using Ret = bool; using T1 = T&; using T2 = void; };

#define CONFIG_FN_GLUE(A, B) A B
#define CONFIG_VA_INVOKE(MACRO, ARGS) CONFIG_FN_GLUE(MACRO, ARGS)

#define CONFIG_GET_MACRO(_1, _2, _3, NAME, ...) NAME

#define CONFIG_EXPAND(X) X

#define CONFIG_DISPATCH_2(FUNC_NAME, TYPE, V1, V2) \
    static_assert(std::is_void_v<FnTraits<TYPE>::T2>, "Put that other param back bud"); \
    typename FnTraits<TYPE>::Ret FUNC_NAME( \
        config::Context& V1, \
        typename FnTraits<TYPE>::T1 V2)

#define CONFIG_DISPATCH_3(FUNC_NAME, TYPE, V1, V2, V3) \
    typename FnTraits<TYPE>::Ret FUNC_NAME( \
        config::Context& V1, \
        typename FnTraits<TYPE>::T1 V2, \
        typename FnTraits<TYPE>::T2 V3)

#define DEFINE_CONFIG_APPLIER(REG, FIELD, ...) \
    CONFIG_EXPAND(CONFIG_VA_INVOKE( \
        CONFIG_EXPAND(CONFIG_GET_MACRO(__VA_ARGS__, CONFIG_DISPATCH_3, CONFIG_DISPATCH_2, )), \
        (REG##Handlers::FIELD##_CB, CONFIG_EXPAND(std::remove_const_t<decltype(REG##Registry::FieldTypeAt<REG::FIELD>::applier)>), __VA_ARGS__) \
    ))

#define DEFINE_CONFIG_VALIDATOR(REG, FIELD, ...) \
    CONFIG_EXPAND(CONFIG_VA_INVOKE( \
        CONFIG_EXPAND(CONFIG_GET_MACRO(__VA_ARGS__, CONFIG_DISPATCH_3, CONFIG_DISPATCH_2, )), \
        (REG##Handlers::FIELD##_VA, CONFIG_EXPAND(std::remove_const_t<decltype(REG##Registry::FieldTypeAt<REG::FIELD>::validator)>), __VA_ARGS__) \
    ))

#endif // REGISTRY_BUILDER_HPP
