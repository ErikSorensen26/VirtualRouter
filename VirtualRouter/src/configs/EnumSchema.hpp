/**
 * @file EnumSchema.hpp
 * @brief Value enums the grammar can name a member of, and the tables that resolve them.
 * @ingroup CONFIG
 *
 * A config field whose type is an enum is set by naming one of its members, not
 * by typing its numeric value: `area 1 stub` means `AreaType::STUB`. The grammar
 * says which member in an "enum" key, and the flattener has to turn that name
 * into the integer the field stores.
 *
 * That needs two things a plain `enum class` cannot give it -- the member names
 * as data, and a way back from the enum's own name as it appears in a grammar
 * file. DEFINE_CONFIG_ENUM emits both from one member list:
 *
 *     #define DUPLEX_MEMBERS(X) X(HALF) X(FULL) X(AUTO)
 *     DEFINE_CONFIG_ENUM(Duplex, DUPLEX_MEMBERS);
 *
 * yields `enum class Duplex { HALF, FULL, AUTO, COUNT }` alongside a
 * `DuplexEnumTable` carrying the type-name hash and one member-name hash per
 * value. A member's value is its position, so the hash table is parallel to the
 * enum and the index the flattener stores *is* the value the field receives.
 *
 * The tables are found through EnumSchema<ENUM>, which DEFINE_CONFIG_ENUM cannot
 * specialize itself: the enums live in nested namespaces (config::ospf) and an
 * explicit specialization has to be written where the primary template is. So
 * registration is the separate REGISTER_CONFIG_ENUM step, used at config scope,
 * and ENUM_ID_LIST in EnumTable.hpp gathers the registered ones so a name with
 * no type in hand can be searched for.
 *
 * @see TupleSchema.hpp, which does the same job for tuple member names.
 */

// EnumSchema.hpp

#ifndef ENUM_SCHEMA_HPP
#define ENUM_SCHEMA_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "configs/ConfigMeta.hpp"

namespace config
{
/**
 * @brief Maps a config value enum to its generated member table.
 *
 * Left undefined for anything REGISTER_CONFIG_ENUM did not name, so a field
 * whose type is a hand written enum reports hasEnumSchemaV == false rather than
 * resolving names against a table that does not exist.
 */
template <typename ENUM>
struct EnumSchema;

/// @brief True when a type is an enum with a registered member table.
template <typename ENUM, typename = void>
inline constexpr bool hasEnumSchemaV = false;

template <typename ENUM>
inline constexpr bool hasEnumSchemaV<ENUM, std::void_t<decltype(EnumSchema<ENUM>::table::members)>> = true;

/// @brief Sentinel for an enum or member name that does not resolve.
inline constexpr uint16_t ENUM_NOT_FOUND = 0xFFFFu;

/// @brief The generated table for a registered enum; ill-formed unless hasEnumSchemaV.
template <typename ENUM>
using EnumTableOf = typename EnumSchema<ENUM>::table;

/**
 * @brief Index of a hashed member name within one enum, or ENUM_NOT_FOUND.
 *
 * The index is the member's value: DEFINE_CONFIG_ENUM assigns no explicit
 * values, so position and value are the same number and the flattener can store
 * what it finds here directly.
 */
template <typename ENUM>
constexpr uint16_t findEnumMember(uint32_t nameHash)
{
    if constexpr (hasEnumSchemaV<ENUM>)
    {
        using Table = EnumTableOf<ENUM>;
        for (std::size_t i = 0; i < Table::count; ++i)
            if (Table::members[i] == nameHash)
                return static_cast<uint16_t>(i);
    }
    return ENUM_NOT_FOUND;
}

/**
 * @brief The member name at an index, for diagnostics; empty when out of range.
 *
 * Names are kept beside the hashes only so a grammar naming a member that does
 * not exist can be told what does. Nothing on the resolution path reads them.
 */
template <typename ENUM>
constexpr std::string_view enumMemberName(uint16_t index)
{
    if constexpr (hasEnumSchemaV<ENUM>)
    {
        using Table = EnumTableOf<ENUM>;
        if (index < Table::count)
            return Table::names[index];
    }
    return {};
}

/// @brief Number of members a registered enum declares; 0 when it has no table.
template <typename ENUM>
constexpr std::size_t enumMemberCount()
{
    if constexpr (hasEnumSchemaV<ENUM>)
        return EnumTableOf<ENUM>::count;
    else
        return 0;
}
}

#define CONFIG_ENUM_MEMBER(NAME)      NAME,
#define CONFIG_ENUM_MEMBER_HASH(NAME) config::tokenHash(#NAME),
#define CONFIG_ENUM_MEMBER_NAME(NAME) std::string_view(#NAME),

#define DEFINE_CONFIG_ENUM(NAME, MEMBER_LIST)                                             \
    enum class NAME { MEMBER_LIST(CONFIG_ENUM_MEMBER) COUNT };                            \
    struct NAME##EnumTable                                                                \
    {                                                                                     \
        using type = NAME;                                                                \
        static constexpr uint32_t typeHash = config::tokenHash(#NAME);                    \
        static constexpr std::string_view typeName = std::string_view(#NAME);             \
        static constexpr std::size_t count = static_cast<std::size_t>(NAME::COUNT);       \
        static constexpr std::array<uint32_t, count> members =                            \
            { MEMBER_LIST(CONFIG_ENUM_MEMBER_HASH) };                                     \
        static constexpr std::array<std::string_view, count> names =                      \
            { MEMBER_LIST(CONFIG_ENUM_MEMBER_NAME) };                                     \
    }

#define DEFINE_CONFIG_ENUM_HERE(NAME, MEMBER_LIST)                                        \
    DEFINE_CONFIG_ENUM(NAME, MEMBER_LIST);                                                \
    template <>                                                                           \
    struct EnumSchema<NAME> { using table = NAME##EnumTable; }

#define DEFINE_CONFIG_ENUM_NS(NS, NAME, MEMBER_LIST)                                      \
    DEFINE_CONFIG_ENUM(NAME, MEMBER_LIST);                                                \
    }                                                                                     \
    template <>                                                                           \
    struct EnumSchema<NS::NAME> { using table = NS::NAME##EnumTable; };                   \
    namespace NS                                                                          \
    {                                                                                     \
    static_assert(hasEnumSchemaV<NAME>, "enum registration did not take effect")

#endif // ENUM_SCHEMA_HPP
