/**
 * @file RegistryTable.hpp
 * @brief The canonical set of configuration registries, and what is derived from it.
 * @ingroup CONFIG
 *
 * REGISTRY_ID_LIST is the one place the set is written down. Everything else here
 * is derived from it rather than maintained beside it: a registry's id is its
 * position in the list, its slots are its enum's COUNT, and the field hash covers
 * the lot. Adding a registry means adding a line, and nothing else.
 */

// RegistryTable.hpp

#ifndef REGISTRY_TABLE_HPP
#define REGISTRY_TABLE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "configs/EnumSchema.hpp"
#include "configs/RegistryTraits.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/registry/global/GlobalRegistry.h"
#include "configs/registry/global/VrfRegistry.h"
#include "configs/registry/interface/ArpRegistry.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "configs/registry/interface/NdpRegistry.h"
#include "configs/registry/policy/AccessListRegistry.hpp"
#include "configs/registry/policy/PrefixListRegistry.hpp"
#include "configs/registry/policy/RouteMapRegistry.h"
#include "configs/registry/router/BgpRegistry.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/registry/router/OspfRegistry.h"

#define REGISTRY_ID_LIST(X) \
    X(Arp) \
    X(Vrf) \
    X(StandardACE) \
    X(StandardACL) \
    X(ExtendedACE) \
    X(ExtendedACL) \
    X(PrefixList) \
    X(NdpBase) \
    X(NdpEntry) \
    X(Ndp) \
    X(RouteMapSequenceBase) \
    X(RouteMapSequence) \
    X(RouteMap) \
    X(Global) \
    X(Eigrp) \
    X(EigrpNamed) \
    X(EigrpInterface) \
    X(BgpTransportBase) \
    X(BgpAfBase) \
    X(BgpNeighbor) \
    X(BgpNeighborSession) \
    X(BgpAddressFamily) \
    X(Bgp) \
    X(Interface) \
    X(OspfArea) \
    X(Ospf) \
    X(Ospfv3AddressFamily)

namespace config
{
template <typename T, uint32_t H>
struct RegistryIdEntry
{
    using type = T;
    static constexpr uint32_t hash = H;
};

template <typename... Ts>
struct RegistryIdList {};

// Leading comma, stripped by CONFIG_STRIP_LEADING_COMMA, so the list can grow
// without a trailing comma dangling at the end of the pack.
#define REGISTRY_ID_ENTRY(ENUM) \
    , RegistryIdEntry<ENUM, tokenHash(#ENUM)>

using RegistryEntries = RegistryIdList<
    CONFIG_STRIP_LEADING_COMMA(REGISTRY_ID_LIST(REGISTRY_ID_ENTRY))
>;

template <typename... Ts, typename F>
constexpr void forEachRegistryId(RegistryIdList<Ts...>, F&& f)
{
    (f.template operator()<Ts>(), ...);
}

/// @brief Number of registries in the list; also the RegistryEntry row count.
template <typename... Ts>
constexpr std::size_t listSize(RegistryIdList<Ts...>) { return sizeof...(Ts); }

inline constexpr std::size_t registryCount = listSize(RegistryEntries{});

/**
 * A registry's id is its position in the list. Searched rather than stored, so
 * adding a registry needs no number written anywhere; a type that is not in the
 * list is a compile error, which is the intent.
 */
template <typename ENUM, typename... Ts>
constexpr uint16_t indexOf(RegistryIdList<Ts...>)
{
    uint16_t i = 0;
    uint16_t found = static_cast<uint16_t>(sizeof...(Ts));
    ((std::is_same_v<ENUM, typename Ts::type> ? found = i : i++), ...);
    return found;
}

template <typename ENUM>
inline constexpr uint16_t registryIdV = indexOf<ENUM>(RegistryEntries{});

template <typename ENUM>
inline constexpr bool isRegisteredV = registryIdV<ENUM> < registryCount;

/**
 * Slots a registry occupies in the slot table. Every config enum terminates
 * with COUNT, so this is uniform across macro generated and hand written enums.
 */
template <typename ENUM>
inline constexpr std::size_t registrySlotsV = static_cast<std::size_t>(ENUM::COUNT);

/// @brief Total slots across every registry, for sizing the flat slot table.
template <typename... Ts>
constexpr std::size_t totalSlots(RegistryIdList<Ts...>)
{
    return (static_cast<std::size_t>(0) + ... + registrySlotsV<typename Ts::type>);
}

inline constexpr std::size_t registrySlotTotal = totalSlots(RegistryEntries{});

/**
 * @brief Where each registry's fields begin in a table laid out over every slot.
 *
 * Entry r is the sum of the slot counts of the registries before it, so registry
 * r's fields occupy [slotBases[r], slotBases[r] + slots(r)). One extra entry at
 * the end holds registrySlotTotal, which makes the end of the last registry
 * readable without a special case.
 */
template <typename... Ts>
constexpr std::array<uint16_t, sizeof...(Ts) + 1> slotBaseTable(RegistryIdList<Ts...>)
{
    std::array<uint16_t, sizeof...(Ts) + 1> bases{};
    std::size_t i = 0;
    uint16_t run = 0;
    ((bases[i++] = run, run += static_cast<uint16_t>(registrySlotsV<typename Ts::type>)), ...);
    bases[i] = run;
    return bases;
}

inline constexpr auto registrySlotBases = slotBaseTable(RegistryEntries{});

static_assert(registrySlotTotal <= 0xFFFFu,
    "slot bases are uint16; a flat table over every field must fit");

/// @brief First slot of a registry known only at runtime; registryCount is out of range.
constexpr uint16_t slotBase(uint16_t registry)
{
    return registry < registryCount ? registrySlotBases[registry] : 0;
}

/// @brief Slot count of a registry known only at runtime, from the base table.
constexpr uint16_t slotSpan(uint16_t registry)
{
    return registry < registryCount
        ? static_cast<uint16_t>(registrySlotBases[registry + 1] - registrySlotBases[registry])
        : 0;
}

/// @brief Sentinel for a name that does not resolve to a registry or field.
inline constexpr uint16_t NOT_FOUND = 0xFFFFu;

/**
 * True when a registry has a generated field-name hash table.
 *
 * DEFINE_CONFIG_GROUP emits one; registries still written by hand do not, so
 * their fields cannot be named from the grammar until they are converted. They
 * still take an id and reserve slots, they just resolve nothing.
 */
template <typename ENUM, typename = void>
inline constexpr bool hasHashesV = false;

template <typename ENUM>
inline constexpr bool hasHashesV<ENUM, std::void_t<decltype(HashRegistry<ENUM>::hashes)>> = true;

/**
 * @brief Registry id for a hashed registry name, or NOT_FOUND.
 *
 * Used by the tree generator to turn the name in the grammar into an id. Linear
 * over a couple dozen entries at build time only.
 */
template <typename... Ts>
constexpr uint16_t findRegistry(RegistryIdList<Ts...>, uint32_t nameHash)
{
    uint16_t i = 0;
    uint16_t found = NOT_FOUND;
    ((Ts::hash == nameHash ? found = i : i++), ...);
    return found;
}

/**
 * @brief Field index for a hashed field name within one registry, or NOT_FOUND.
 *
 * NAME##Hashes[] is parallel to the enum and terminated by tokenHash("COUNT"),
 * so the search stops before the terminator.
 */
template <typename ENUM>
constexpr uint16_t findField(uint32_t nameHash)
{
    if constexpr (hasHashesV<ENUM>)
    {
        auto& hashes = HashRegistry<ENUM>::hashes;
        for (std::size_t i = 0; i < registrySlotsV<ENUM>; ++i)
            if (hashes[i] == nameHash)
                return static_cast<uint16_t>(i);
    }
    return NOT_FOUND;
}

/**
 * @brief Resolves a field hash against a registry known only at runtime.
 *
 * The generator has an id parsed from the grammar, not a type, so this walks the
 * list to recover the type and then searches that registry's hashes.
 */
template <typename... Ts>
constexpr uint16_t findFieldIn(RegistryIdList<Ts...>, uint16_t registry, uint32_t nameHash)
{
    uint16_t i = 0;
    uint16_t found = NOT_FOUND;
    // A lambda, not a conditional: a conditional expression is not a valid
    // fold operand, and wrapping it in parentheses does not change that.
    ([&]{ if (i++ == registry) found = findField<typename Ts::type>(nameHash); }(), ...);
    return found;
}

/**
 * @brief Tuple member index for a field known only at runtime, or TUPLE_NOT_FOUND.
 *
 * findTupleMember needs the field as a template argument, but the flattener has
 * an index parsed from the grammar. This turns the one into the other: expand
 * over the registry's field values, keep the branch whose value matches, and ask
 * that field's schema. Fields with no schema answer TUPLE_NOT_FOUND, which is
 * what a grammar naming a member of a non-tuple field should get.
 */
template <typename ENUM, std::size_t... Is>
constexpr uint16_t findTupleMemberInField(uint16_t field, uint32_t nameHash,
                                          std::index_sequence<Is...>)
{
    uint16_t found = TUPLE_NOT_FOUND;
    ([&]{
        if (field == Is)
            found = findTupleMember<ENUM, static_cast<ENUM>(Is)>(nameHash);
    }(), ...);
    return found;
}

template <typename ENUM>
constexpr uint16_t findTupleMemberIn(uint16_t field, uint32_t nameHash)
{
    return findTupleMemberInField<ENUM>(
        field, nameHash, std::make_index_sequence<registrySlotsV<ENUM>>{});
}

/**
 * @brief As above, for a registry that is also only known at runtime.
 *
 * Mirrors findFieldIn: walk the list to recover the registry type from its id,
 * then resolve the field within it.
 */
template <typename... Ts>
constexpr uint16_t findTupleMemberAt(RegistryIdList<Ts...>, uint16_t registry,
                                     uint16_t field, uint32_t nameHash)
{
    uint16_t i = 0;
    uint16_t found = TUPLE_NOT_FOUND;
    ([&]{
        if (i++ == registry)
            found = findTupleMemberIn<typename Ts::type>(field, nameHash);
    }(), ...);
    return found;
}

/**
 * @brief What a grammar's "Type::MEMBER" resolved to against one field.
 *
 * Separated from a bare index because the two ways it can fail need telling
 * apart: naming the wrong enum entirely is a different grammar mistake from
 * naming a member the right enum does not have, and the flattener reports them
 * differently.
 */
struct EnumResolution
{
    uint16_t index = ENUM_NOT_FOUND;   ///< The member's value, when it resolved.
    bool     fieldIsEnum = false;      ///< The field's type has a member table.
    bool     typeMatched = false;      ///< The named type is that field's type.
    std::string_view fieldTypeName;    ///< For the diagnostic when it is not.
};

/**
 * @brief Resolves an enum member against the type a field actually declares.
 *
 * The grammar names both halves -- "Duplex::FULL" -- and the type half is
 * checked rather than trusted. A command's enum key sits beside a config key
 * binding it to a field, so the field's own type is the authority on which enum
 * is correct; naming a different one is caught here instead of writing a value
 * from an unrelated enum into the field.
 */
template <typename FieldT>
constexpr EnumResolution resolveEnumOnField(uint32_t typeHash, uint32_t memberHash)
{
    EnumResolution out;

    if constexpr (requires { typename FieldT::type; })
    {
        using Value = std::remove_cvref_t<typename FieldT::type>;

        if constexpr (hasEnumSchemaV<Value>)
        {
            out.fieldIsEnum  = true;
            out.fieldTypeName = EnumTableOf<Value>::typeName;
            out.typeMatched  = (EnumTableOf<Value>::typeHash == typeHash);

            if (out.typeMatched)
                out.index = findEnumMember<Value>(memberHash);
        }
    }
    return out;
}

/**
 * True when a registry has a generated field tuple to index into.
 *
 * DEFINE_CONFIG_GROUP emits the RegistryOf specialization; registries still
 * written by hand have none, so their field types cannot be recovered from an
 * index and an enum cannot be checked against them. Same story as hasHashesV.
 */
template <typename ENUM, typename = void>
inline constexpr bool hasFieldsV = false;

template <typename ENUM>
inline constexpr bool hasFieldsV<ENUM, std::void_t<typename RegistryOf<ENUM>::type>> = true;

template <typename ENUM, std::size_t... Is>
constexpr EnumResolution resolveEnumInField(uint16_t field, uint32_t typeHash,
                                            uint32_t memberHash, std::index_sequence<Is...>)
{
    EnumResolution out;
    if constexpr (hasFieldsV<ENUM>)
    {
        ([&]{
            if (field == Is)
                out = resolveEnumOnField<
                    typename RegistryOf<ENUM>::type::template FieldTypeAt<static_cast<ENUM>(Is)>>(
                        typeHash, memberHash);
        }(), ...);
    }
    return out;
}

template <typename ENUM>
constexpr EnumResolution resolveEnumIn(uint16_t field, uint32_t typeHash, uint32_t memberHash)
{
    return resolveEnumInField<ENUM>(
        field, typeHash, memberHash, std::make_index_sequence<registrySlotsV<ENUM>>{});
}

/// @brief As above, for a registry known only by its runtime id.
template <typename... Ts>
constexpr EnumResolution resolveEnumAt(RegistryIdList<Ts...>, uint16_t registry,
                                       uint16_t field, uint32_t typeHash, uint32_t memberHash)
{
    uint16_t i = 0;
    EnumResolution out;
    ([&]{
        if (i++ == registry)
            out = resolveEnumIn<typename Ts::type>(field, typeHash, memberHash);
    }(), ...);
    return out;
}

/**
 * Identifies the registry list *and* every field in it. Folds each registry's
 * field hashes in after its name, so inserting, renaming or reordering a field
 * inside a FIELD_LIST changes the value. Without that, a field inserted mid list
 * would shift every index below it while leaving the file hash untouched, and a
 * stale binary would bind commands to the wrong fields.
 */
template <typename ENUM>
constexpr uint32_t foldFields(uint32_t h)
{
    if constexpr (hasHashesV<ENUM>)
    {
        auto& hashes = HashRegistry<ENUM>::hashes;
        for (std::size_t i = 0; i < registrySlotsV<ENUM>; ++i)
        {
            h ^= hashes[i];
            h *= 0x01000193;
        }
    }
    else
    {
        // No hash table, but its slot count still shifts every id behind it.
        h ^= static_cast<uint32_t>(registrySlotsV<ENUM>);
        h *= 0x01000193;
    }
    return h;
}

template <typename... Ts>
constexpr uint32_t listFieldHash(RegistryIdList<Ts...>)
{
    uint32_t h = 0x811C9DC5;
    ((h ^= Ts::hash, h *= 0x01000193,
      h = foldFields<typename Ts::type>(h)), ...);
    return h;
}

inline constexpr uint32_t REGISTRY_FIELD_HASH = listFieldHash(RegistryEntries{});

}

#endif // REGISTRY_TABLE_HPP
