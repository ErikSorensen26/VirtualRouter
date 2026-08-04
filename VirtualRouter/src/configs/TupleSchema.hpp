/**
 * @file TupleSchema.hpp
 * @brief Named members over a std::tuple, and the tables that resolve those names.
 * @ingroup CONFIG
 *
 * A list-valued config field stores a tuple -- an OSPF area range is a prefix, an
 * advertise flag and a cost -- and the grammar sets one member of it at a time:
 * `area 1 range A.B.C.D M.M.M.M cost 100` writes the third. Naming the member
 * needs the member names as data, which a bare std::tuple does not carry, so
 * DEFINE_TUPLE_SCHEMA emits them beside the accessors it already generated.
 *
 * The reverse map is TupleSchemaFor<ENUM, E>: the flattener resolves a field and
 * then has to reach the schema that named its members. It is keyed on the field
 * and not on the field's type, because Tuple is structural -- OspfTrafEngInterface
 * and RouteMapMetricRange are both tuple<uint32_t, uint32_t> -- and a type-keyed
 * map could only ever answer one of them for both. TUPLE_SCHEMA_FOR states the
 * pairing at the field, where the two stay distinct.
 */

// TupleSchema.hpp

#ifndef TUPLE_SCHEMA_HPP
#define TUPLE_SCHEMA_HPP

#include <array>
#include <tuple>
#include <utility>      // std::declval
#include <cstddef>      // std::size_t
#include <cstdint>
#include <string_view>
#include <type_traits>  // std::tuple_element_t

#include "configs/ConfigMeta.hpp"

/**
 * Usage:
 *
 *   #define MY_FIELDS(X) \
 *       X(int,    Foo)   \
 *       X(double, Bar)
 *
 *   DEFINE_TUPLE_SCHEMA(MySchema, MY_FIELDS);
 *
 *   MySchema::Tuple t{ 1, 3.14 };
 *   auto& foo = MySchema::Foo(t);
 *
 * If a field type contains a top level comma the preprocessor would read it as
 * two arguments, so parenthesize it. The parentheses are stripped by TS_TYPE
 * and are not part of the resulting type.
 *
 *       X((std::variant<A, B>), Foo)
 */
namespace config::ts
{
/**
 * void(T) is a function type taking one parameter; peel T back out of it.
 * This is what lets a parenthesized (T) survive as a single macro argument.
 */
template <typename> struct Unparen;
template <typename T> struct Unparen<void(T)> { using type = T; };

template <typename T> using Unparen_t = typename Unparen<T>::type;
}

namespace config
{
/**
 * @brief Maps one registry field to the schema naming its tuple's members.
 *
 * Keyed on the field rather than on its tuple type, because a tuple type does
 * not identify a schema: Tuple is a plain std::tuple, so two schemas over the
 * same element types are indistinguishable. Specialized by TUPLE_SCHEMA_FOR,
 * and left undefined otherwise so a tuple-valued field with no schema declared
 * reports hasTupleSchemaV == false rather than resolving member names against
 * some other field's table.
 */
template <typename ENUM, ENUM E>
struct TupleSchemaFor;

/// @brief True when a field has a schema naming its tuple members.
template <typename ENUM, ENUM E, typename = void>
inline constexpr bool hasTupleSchemaV = false;

template <typename ENUM, ENUM E>
inline constexpr bool hasTupleSchemaV<ENUM, E, std::void_t<decltype(TupleSchemaFor<ENUM, E>::type::members)>> = true;

/// @brief The schema for a field; ill-formed unless hasTupleSchemaV.
template <typename ENUM, ENUM E>
using TupleSchemaT = typename TupleSchemaFor<ENUM, E>::type;

/// @brief Sentinel for a tuple member name that does not resolve.
inline constexpr uint16_t TUPLE_NOT_FOUND = 0xFFFFu;

/**
 * @brief Index of a hashed member name within a field's schema, or TUPLE_NOT_FOUND.
 *
 * The index is the member's position in the tuple, which is what std::get takes,
 * so what the flattener stores is directly what the executor indexes with.
 */
template <typename ENUM, ENUM E>
constexpr uint16_t findTupleMember(uint32_t nameHash)
{
    if constexpr (hasTupleSchemaV<ENUM, E>)
    {
        using Schema = TupleSchemaT<ENUM, E>;
        for (std::size_t i = 0; i < Schema::Count; ++i)
            if (Schema::members[i] == nameHash)
                return static_cast<uint16_t>(i);
    }
    return TUPLE_NOT_FOUND;
}

/// @brief The member name at an index, for diagnostics; empty when out of range.
template <typename ENUM, ENUM E>
constexpr std::string_view tupleMemberName(uint16_t index)
{
    if constexpr (hasTupleSchemaV<ENUM, E>)
    {
        using Schema = TupleSchemaT<ENUM, E>;
        if (index < Schema::Count)
            return Schema::names[index];
    }
    return {};
}
}

// Accepts either a bare type or a parenthesized one: TS_TYPE(int), TS_TYPE((A<x,y>)).
#define TS_TYPE(T) config::ts::Unparen_t<void(T)>

#define TS_INDEX_ELEM(T, Name)    Index_##Name,
#define TS_TUPLE_ELEM(T, Name)    std::declval<std::tuple<TS_TYPE(T)>>(),
#define TS_HASH_ELEM(T, Name)     config::tokenHash(#Name),
#define TS_NAME_ELEM(T, Name)     std::string_view(#Name),

#define TS_ACCESSOR_ELEM(T, Name)                                                         \
    static TS_TYPE(T)& Name(Tuple& t) noexcept {                                          \
        return std::get<static_cast<std::size_t>(Index::Index_##Name)>(t);                \
    }                                                                                     \
    static const TS_TYPE(T)& Name(const Tuple& t) noexcept {                              \
        return std::get<static_cast<std::size_t>(Index::Index_##Name)>(t);                \
    }

/**
 * @brief Declares a named-member view over a tuple, plus its name tables.
 *
 * `members` is parallel to the tuple and holds one name hash per element, which
 * is what a grammar's "Registry::field::member" resolves against. `names` is
 * kept beside it for diagnostics only.
 *
 * The TupleSchemaOf specialization is emitted unqualified, so this must be used
 * from inside namespace config itself. Every registry header already is: the
 * nested namespaces in them (config::ospf, config::bgp) wrap only the
 * hand written value enums, and close before the schemas. Use
 * REGISTER_TUPLE_SCHEMA instead for a schema that does live in a nested one.
 */
#define DEFINE_TUPLE_SCHEMA(Schema, FIELD_LIST)                                           \
    struct Schema final {                                                                 \
        enum class Index : std::size_t {                                                  \
            FIELD_LIST(TS_INDEX_ELEM)                                                     \
            Count                                                                         \
        };                                                                                \
                                                                                          \
        using Tuple = decltype(std::tuple_cat(                                            \
            FIELD_LIST(TS_TUPLE_ELEM)                                                     \
            std::declval<std::tuple<>>()                                                  \
        ));                                                                               \
                                                                                          \
        static constexpr std::size_t Count = static_cast<std::size_t>(Index::Count);      \
                                                                                          \
        static constexpr std::string_view schemaName = std::string_view(#Schema);         \
                                                                                          \
        static constexpr std::array<uint32_t, Count> members =                            \
            { FIELD_LIST(TS_HASH_ELEM) };                                                 \
        static constexpr std::array<std::string_view, Count> names =                      \
            { FIELD_LIST(TS_NAME_ELEM) };                                                 \
                                                                                          \
        template <Index I>                                                                \
        using FieldType = std::tuple_element_t<static_cast<std::size_t>(I), Tuple>;       \
                                                                                          \
        FIELD_LIST(TS_ACCESSOR_ELEM)                                                      \
    }

/**
 * @brief Names the schema a field's members are resolved against.
 *
 * Placed in the field list beside the field it describes:
 *
 *     LIST_FIELD_TUPLE(X, Y, RANGE, OspfAreaRange)
 *
 * The schema cannot be recovered from the field's type alone. A schema's Tuple
 * is structural, so two unrelated schemas over the same element types are the
 * same type -- OspfTrafEngInterface and RouteMapMetricRange are both
 * tuple<uint32_t, uint32_t> -- and a type-keyed map would have to answer one of
 * them for both. Naming the schema at the field keeps the two apart.
 */
#define TUPLE_SCHEMA_FOR(FIELD_ENUM, VALUE, SCHEMA)                                       \
    template <>                                                                           \
    struct TupleSchemaFor<FIELD_ENUM, VALUE> { using type = SCHEMA; }

#endif // TUPLE_SCHEMA_HPP
