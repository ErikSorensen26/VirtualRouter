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
 * No reverse map is needed to get back. A field declares the schema itself and
 * stores it directly -- SCHEMA derives from its Tuple (see DEFINE_TUPLE_SCHEMA)
 * rather than wrapping one, so the field's stored type already is the schema.
 * Declaring the Tuple directly would lose the name: Tuple is structural --
 * OspfTrafEngInterface and RouteMapMetricRange are both tuple<uint32_t,
 * uint32_t> -- while the schemas naming them are distinct types.
 */

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
#include "configs/RegistryTraits.hpp"

namespace config
{
inline constexpr uint16_t TUPLE_NOT_FOUND = 0xFFFFu;

namespace ts
{
template <typename> struct Unparen;
template <typename T> struct Unparen<void(T)> { using type = T; };
template <typename T> using Unparen_t = typename Unparen<T>::type;

template <size_t Count, std::size_t TableSize>
constexpr std::array<uint16_t, TableSize> buildMemberTable(const std::array<uint32_t, Count>& hashes)
{
    std::array<uint16_t, TableSize> table{};
    for (auto& slot : table)
        slot = TUPLE_NOT_FOUND;
    constexpr std::size_t mask = TableSize - 1;
    for (std::size_t i =0; i < Count; ++i)
    {
        std::size_t slot = hashes[i] & mask;
        while (table[slot] != TUPLE_NOT_FOUND)
            slot = (slot + 1) & mask;
        table[slot] = static_cast<uint16_t>(i);
    }
    return table;
}
}

template <typename T, typename = void>
inline constexpr bool hasTupleSchemaV = false;

template <typename T>
inline constexpr bool hasTupleSchemaV<T, std::void_t<decltype(T::members)>> = true;

namespace ts
{
/**
 * @brief The literal std::tuple<Ts...> a type is, or that a schema derives from.
 *
 * tuple_size/tuple_element are keyed on the exact std::tuple<Ts...>
 * specialization and do not see through inheritance, so code that needs
 * either trait on a value that might be a schema has to resolve to
 * TupleBaseOf first. Written as a partial specialization (like @ref Storage) rather than
 * std::conditional_t, whose branches are both named even when only one is
 * selected -- fatal for the branch naming T::Tuple when T is a bare tuple.
 */
template <typename T, typename = void>
struct TupleBase { using type = T; };

template <typename T>
struct TupleBase<T, std::enable_if_t<hasTupleSchemaV<T>>> { using type = typename T::Tuple; };
}

/// @brief The literal std::tuple<Ts...> behind T; T itself when T already is one.
template <typename T>
using TupleBaseOf = typename ts::TupleBase<T>::type;

template <typename T>
constexpr uint16_t findTupleMember(uint32_t nameHash)
{
    if constexpr (hasTupleSchemaV<T>)
    {
        constexpr size_t mask = T::TableSize - 1;
        std::size_t slot = nameHash & mask;
        for (size_t probes = 0; probes < T::TableSize; ++probes)
        {
            const uint16_t idx = T::memberTable[slot];
            if (idx == TUPLE_NOT_FOUND)
                return TUPLE_NOT_FOUND;
            if (T::members[idx] == nameHash)
                return idx;
            slot = (slot + 1) & mask;
        }
    }
    return TUPLE_NOT_FOUND;
}

template <typename T>
constexpr std::string_view tupleMemberName(uint16_t index)
{
    if constexpr (hasTupleSchemaV<T>)
    {
        if (index < T::count)
            return T::names[index];
    }
    return {};
}

template <typename T>
constexpr size_t tupleMemberCount()
{
    if constexpr (hasTupleSchemaV<T>)
        return T::count;
    else
        return 0;
}

struct TupleResolution
{
    uint16_t index = TUPLE_NOT_FOUND;
    bool typeMatched = false;
    std::string_view typeName;
};

template <typename T>
constexpr TupleResolution resolveTupleSchema(uint32_t typeHash, uint32_t memberHash)
{
    TupleResolution out;
    if constexpr (hasTupleSchemaV<T>)
    {
        out.typeName = T::typeName;
        out.typeMatched = (T::typeHash == typeHash);
        if (out.typeMatched)
            out.index = findTupleMember<T>(memberHash);
    }
    return out;
}
}

#define TS_TYPE(T) config::ts::Unparen_t<void(T)>

#define TS_INDEX_ELEM(T, NAME) Index_##NAME,
#define TS_TUPLE_ELEM(T, NAME) std::declval<std::tuple<TS_TYPE(T)>>(),
#define TS_HASH_ELEM(T, NAME)  config::tokenHash(#NAME),
#define TS_NAME_ELEM(T, NAME)  std::string_view(#NAME),

#define TS_ACCESSOR_ELEM(T, NAME)                                                         \
    TS_TYPE(T)& NAME() noexcept {                                                          \
        return std::get<static_cast<size_t>(Index::Index_##NAME)>(*this); }                \
    const TS_TYPE(T)& NAME() const noexcept {                                              \
        return std::get<static_cast<size_t>(Index::Index_##NAME)>(*this); }

#define DEFINE_TUPLE_SCHEMA(SCHEMA, FIELD_LIST)                                           \
    using SCHEMA##_TupleT = decltype(std::tuple_cat(FIELD_LIST(TS_TUPLE_ELEM)             \
                                          std::declval<std::tuple<>>()));                  \
    struct SCHEMA final : SCHEMA##_TupleT {                                               \
        using Tuple = SCHEMA##_TupleT;                                                    \
        using Tuple::Tuple;                                                               \
        enum class Index : std::size_t { FIELD_LIST(TS_INDEX_ELEM) Count };               \
        static constexpr size_t count = static_cast<size_t>(Index::Count);                \
        static constexpr std::string_view typeName = std::string_view(#SCHEMA);           \
        static constexpr uint32_t typeHash = config::tokenHash(#SCHEMA);                  \
        static constexpr std::array<uint32_t, count> members =                            \
            { FIELD_LIST(TS_HASH_ELEM) };                                                 \
        static constexpr std::array<std::string_view, count> names =                      \
            { FIELD_LIST(TS_NAME_ELEM) };                                                 \
        static constexpr size_t TableSize = std::bit_ceil(count * 2);                     \
        static constexpr std::array<uint16_t, TableSize> memberTable =                    \
            config::ts::buildMemberTable<count, TableSize>(members);                      \
        template <Index I>                                                                \
        using FieldType = std::tuple_element_t<static_cast<size_t>(I), Tuple>;            \
        FIELD_LIST(TS_ACCESSOR_ELEM)                                                      \
    }

#endif // TUPLE_SCHEMA_HPP
