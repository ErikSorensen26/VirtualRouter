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

#include <type_traits>
#include <string_view>
#include <cstdint>

#include "configs/RegistryTraits.hpp" // IWYU pragma: keep
#include "configs/RegistryDefaultTable.hpp" // IWYU pragma: keep
#include "configs/SubRegistry.hpp" // IWYU pragma: keep
#include "configs/RegistryReference.hpp" // IWYU pragma: keep

#define TODO /*TODO*/

#define CONFIG_CAT_(a, b) a##b
#define CONFIG_CAT(a, b) CONFIG_CAT_(a, b)

#define CONFIG_TAIL(first, ...) __VA_ARGS__
#define CONFIG_STRIP_LEADING_COMMA(...) CONFIG_TAIL(__VA_ARGS__)

#define CONFIG_FIELD(X, E, KIND, NAME, TYPE)                 X(E, KIND, NAME, TYPE, 0, ~, 0, ~, ~)
#define CONFIG_FIELD_CB(X, E, KIND, NAME, TYPE, CB)          X(E, KIND, NAME, TYPE, 0, ~, 1, CB, ~)
#define CONFIG_FIELD_DEF(X, E, KIND, NAME, TYPE, DEF)        X(E, KIND, NAME, TYPE, 1, DEF, 0, ~, ~)
#define CONFIG_FIELD_DEF_CB(X, E, KIND, NAME, TYPE, DEF, CB) X(E, KIND, NAME, TYPE, 1, DEF, 1, CB, ~)

// OwnedListField
#define OWNED_LIST_FIELD(X, E, NAME, TYPE, KEY)              X(E, OwnedListField, NAME, TYPE, 0, ~, 2, ~, KEY)
#define OWNED_LIST_FIELD_CB(X, E, NAME, TYPE, KEY, CB)       X(E, OwnedListField, NAME, TYPE, 0, ~, 3, CB, KEY)

// AtomicField
#define ATOMIC_FIELD(X, E, NAME, TYPE, DEF) CONFIG_FIELD_DEF(X, E, AtomicField, NAME, TYPE, DEF)
#define ATOMIC_FIELD_CB(X, E, NAME, TYPE, DEF, CB) CONFIG_FIELD_DEF_CB(X, E, AtomicField, NAME, TYPE, DEF, CB)

// OptionalAtomicField
#define OPTIONAL_ATOMIC_FIELD(X, E, NAME, TYPE) CONFIG_FIELD(X, E, OptionalAtomicField, NAME, TYPE)
#define OPTIONAL_ATOMIC_FIELD_CB(X, E, NAME, TYPE, CB) CONFIG_FIELD_CB(X, E, OptionalAtomicField, NAME, TYPE, CB)

// ListField
#define LIST_FIELD(X, E, NAME, TYPE) CONFIG_FIELD(X, E, ListField, NAME, TYPE)
#define LIST_FIELD_CB(X, E, NAME, TYPE, CB) CONFIG_FIELD_CB(X, E, ListField, NAME, TYPE, CB)

// ValueField
#define VALUE_FIELD(X, E, NAME, TYPE) CONFIG_FIELD(X, E, ValueField, NAME, TYPE)
#define VALUE_FIELD_CB(X, E, NAME, TYPE, CB) CONFIG_FIELD_CB(X, E, ValueField, NAME, TYPE, CB)

// RegistryContainer
#define REGISTRY_CONTAINER(X, E, NAME, TYPE) CONFIG_FIELD(X, E, RegistryContainer, NAME, TYPE)

// OptionalRegistryContainer
#define OPTIONAL_REGISTRY_CONTAINER(X, E, NAME, TYPE) CONFIG_FIELD(X, E, OptionalRegistryContainer, NAME, TYPE)

#define CONFIG_ENUM_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, SPEC, CB, KEY) NAME,
#define CONFIG_HASH_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, SPEC, CB, KEY) tokenHash(#NAME),

#define CONFIG_DEF_ENTRY_SPEC_1(E, NAME, DEF) \
    template <> \
    struct Entry<E, E::NAME> \
    { \
        static constexpr bool has = true; \
        template <typename T> \
        static constexpr T get() noexcept \
        { \
            return static_cast<T>(DEF); \
        } \
    };
#define CONFIG_DEF_ENTRY_SPEC_0(E, NAME, DEF)

#define CONFIG_DEF_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, SPEC, CB, KEY) \
    CONFIG_CAT(CONFIG_DEF_ENTRY_SPEC_, HASDEF)(E, NAME, DEF)

#define CONFIG_FIELD_SPEC_0(KIND, TYPE, E, NAME, CB, KEY) KIND<TYPE CONFIG_INDEX_ARG(E::NAME)>
#define CONFIG_FIELD_SPEC_1(KIND, TYPE, E, NAME, CB, KEY) KIND<TYPE CONFIG_INDEX_ARG(E::NAME), CB>
#define CONFIG_FIELD_SPEC_2(KIND, TYPE, E, NAME, CB, KEY) KIND<TYPE, KEY CONFIG_INDEX_ARG(E::NAME)>
#define CONFIG_FIELD_SPEC_3(KIND, TYPE, E, NAME, CB, KEY) KIND<TYPE, KEY CONFIG_INDEX_ARG(E::NAME), CB>

#define CONFIG_FIELDTUPLE_ENTRY(E, KIND, NAME, TYPE, HASDEF, DEF, SPEC, CB, KEY) \
    , CONFIG_CAT(CONFIG_FIELD_SPEC_, SPEC)(KIND, TYPE, E, NAME, CB, KEY)

#define DEFINE_CONFIG_GROUP(NAME, LIST) \
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
    struct NAME##Registry : SubRegistry<NAME##Registry, NAME, nullptr, NAME##Fields> {}; \
    template <> \
    struct RegistryOf<NAME> { using type = NAME##Registry; };

#endif // REGISTRY_BUILDER_HPP
