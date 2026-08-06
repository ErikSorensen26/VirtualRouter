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

#include <EnumBitMap.hpp>

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
    ([&]{ if (i++ == registry) found = findField<typename Ts::type>(nameHash); }(), ...);
    return found;
}

/**
 * True when a registry has a generated field tuple to index into.
 *
 * DEFINE_CONFIG_GROUP emits the RegistryOf specialization; registries still
 * written by hand have none, so their field types cannot be recovered from an
 * index. Same story as hasHashesV.
 */
template <typename ENUM, typename = void>
inline constexpr bool hasFieldsV = false;

template <typename ENUM>
inline constexpr bool hasFieldsV<ENUM, std::void_t<typename RegistryOf<ENUM>::type>> = true;

/**
 * @brief Calls f.template operator()<FieldT>() for a field named at runtime.
 *
 * Every lookup below starts the same way: the grammar supplies a registry id and
 * a field index, and the answer needs the field's type. Recovering it means two
 * expansions -- one over the registry list, one over that registry's fields --
 * which is the bulk of what each lookup would otherwise repeat. It is written
 * once here so each of them is only the question it actually asks.
 */
template <typename ENUM, typename F, std::size_t... Is>
constexpr void visitFieldIn(uint16_t field, F&& f, std::index_sequence<Is...>)
{
    if constexpr (hasFieldsV<ENUM>)
        ([&]{
            if (field == Is)
                f.template operator()<typename RegistryOfT<ENUM>::template FieldTypeAt<static_cast<ENUM>(Is)>>();
        }(), ...);
}

template <typename... Ts, typename F>
constexpr void visitField(RegistryIdList<Ts...>, uint16_t registry, uint16_t field, F&& f)
{
    uint16_t i = 0;
    ([&]{
        if (i++ == registry)
            visitFieldIn<typename Ts::type>(field, f,
                std::make_index_sequence<registrySlotsV<typename Ts::type>>{});
    }(), ...);
}

/**
 * @brief The tuple schema a field stores; the field itself when it stores none.
 *
 * Only fields holding a list or a value name a node, so the alias has to stay
 * unevaluated for every other kind rather than resolve to a missing member.
 */
namespace rt
{
template <typename FieldT, typename = void>
struct Schema { using type = FieldT; };

template <typename FieldT>
struct Schema<FieldT, std::void_t<typename FieldT::node>> { using type = typename FieldT::node; };
}

template <typename FieldT>
using SchemaOf = typename rt::Schema<FieldT>::type;

template <typename FieldT>
inline constexpr bool fieldHasSchemaV = hasTupleSchemaV<SchemaOf<FieldT>>;

/// @brief Where a "Schema::member" spelling's schema is stored, and how uniquely.
struct TupleFieldLookup
{
    uint16_t registry = NOT_FOUND;
    uint16_t field = NOT_FOUND;
    uint16_t count = 0;    ///< Fields storing the schema; >1 leaves the field to the caller.

    /**
     * @brief True when every match is in one registry.
     *
     * Fields storing the same schema name their members alike, so a member
     * resolves the same way through any of them. What differs is which field is
     * written, and a caller naming that itself -- as distribute-list does, with
     * a field per direction -- is not ambiguous, only unresolved here.
     */
    bool oneRegistry = true;
};

/**
 * @brief Finds the field storing a named tuple schema.
 *
 * The two-part "Schema::member" spelling names no field, so the field is what
 * has to be recovered. A field declares the schema itself rather than its Tuple
 * precisely so this is answerable: Tuple is structural and two schemas sharing a
 * shape would be indistinguishable through it.
 *
 * Every match is counted rather than the first returned, because a schema stored
 * by more than one field makes the short form ambiguous and the caller reports
 * that instead of picking one.
 */
/// @brief Counts the fields of one registry storing a named schema, into out.
template <typename ENUM, std::size_t... Is>
constexpr void countSchemaIn(TupleFieldLookup& out, uint16_t registry, uint32_t typeHash,
                             std::index_sequence<Is...>)
{
    if constexpr (hasFieldsV<ENUM>)
        ([&]{
            using FieldT = typename RegistryOfT<ENUM>::template FieldTypeAt<static_cast<ENUM>(Is)>;
            if constexpr (fieldHasSchemaV<FieldT>)
                if (SchemaOf<FieldT>::typeHash == typeHash)
                {
                    if (out.count == 0) { out.registry = registry; out.field = static_cast<uint16_t>(Is); }
                    else if (out.registry != registry) out.oneRegistry = false;
                    ++out.count;
                }
        }(), ...);
}

template <typename... Ts>
constexpr TupleFieldLookup findTupleField(RegistryIdList<Ts...>, uint32_t typeHash)
{
    TupleFieldLookup out;

    uint16_t reg = 0;
    (countSchemaIn<typename Ts::type>(out, reg++, typeHash,
        std::make_index_sequence<registrySlotsV<typename Ts::type>>{}), ...);

    return out;
}

/// @brief Member index within the schema a runtime-named field stores.
template <typename... Ts>
constexpr uint16_t findTupleMemberAt(RegistryIdList<Ts...> list, uint16_t registry,
                                     uint16_t field, uint32_t nameHash)
{
    uint16_t found = TUPLE_NOT_FOUND;
    visitField(list, registry, field, [&]<typename FieldT>{
        if constexpr (fieldHasSchemaV<FieldT>)
            found = findTupleMember<SchemaOf<FieldT>>(nameHash);
    });
    return found;
}

/**
 * @brief What a grammar's "Type::MEMBER" resolved to against one field.
 *
 * Separated from a bare index because the two ways it can fail need telling
 * apart: naming the wrong enum entirely is a different grammar mistake from
 * naming a member the right enum does not have.
 */
struct EnumResolution
{
    uint16_t index = ENUM_NOT_FOUND;   ///< The member's value, when it resolved.
    bool     fieldIsEnum = false;      ///< The type has a member table.
    bool     typeMatched = false;      ///< The named type is that type.
    bool     isBitMap = false;         ///< The field stores a set of members, not one.
    std::string_view fieldTypeName;    ///< For the diagnostic when it is not.
};

/**
 * @brief Resolves "Type::MEMBER" against one known field type.
 *
 * A bitmap field is named by the enum it is indexed by rather than by its
 * storage integer, so the enum is what the member is resolved against and the
 * caller is told to set a bit instead of writing the value.
 */
template <typename T>
constexpr EnumResolution resolveEnumOn(uint32_t typeHash, uint32_t memberHash)
{
    using Value = typename types::EnumBitMapEnumOr<T>::type;

    EnumResolution out;
    if constexpr (hasEnumSchemaV<Value>)
    {
        out.fieldIsEnum   = true;
        out.isBitMap      = types::isEnumBitMapV<T>;
        out.fieldTypeName = EnumTableOf<Value>::typeName;
        out.typeMatched   = (EnumTableOf<Value>::typeHash == typeHash);

        if (out.typeMatched)
            out.index = findEnumMember<Value>(memberHash);
    }
    return out;
}

/**
 * @brief Resolves an enum member against the type a field actually declares.
 *
 * The grammar names both halves -- "Duplex::FULL" -- and the type half is
 * checked rather than trusted, so naming an unrelated enum is caught here
 * instead of writing its value into the field.
 *
 * @param member Tuple member index, or TUPLE_NOT_FOUND for the field itself.
 *               A field storing a tuple is never an enum, so on a tuple member
 *               the member's own type is the authority.
 */
template <typename... Ts>
constexpr EnumResolution resolveEnumAt(RegistryIdList<Ts...> list, uint16_t registry,
                                       uint16_t field, uint16_t member,
                                       uint32_t typeHash, uint32_t memberHash)
{
    EnumResolution out;
    visitField(list, registry, field, [&]<typename FieldT>{
        if (member == TUPLE_NOT_FOUND)
        {
            if constexpr (requires { typename FieldT::type; })
                out = resolveEnumOn<std::remove_cvref_t<typename FieldT::type>>(typeHash, memberHash);
        }
        else if constexpr (fieldHasSchemaV<FieldT>)
        {
            using Schema = SchemaOf<FieldT>;
            [&]<std::size_t... Is>(std::index_sequence<Is...>)
            {
                ([&]{
                    if (member == Is)
                        out = resolveEnumOn<std::remove_cvref_t<
                            typename Schema::template FieldType<static_cast<typename Schema::Index>(Is)>>>(
                                typeHash, memberHash);
                }(), ...);
            }(std::make_index_sequence<Schema::count>{});
        }
    });
    return out;
}

/// @brief Whether a field rescopes into another registry, and which one.
struct FieldScope
{
    bool     isContainer = false;
    uint16_t registry = NOT_FOUND;
};

/**
 * @brief Reports whether a runtime-named field holds a child registry scope.
 *
 * A command binding one of these does not write a value, it moves where the
 * commands under it write, so the generator needs the registry the entries live
 * in rather than the field's own.
 */
template <typename... Ts>
constexpr FieldScope resolveScopeAt(RegistryIdList<Ts...> list, uint16_t registry, uint16_t field)
{
    FieldScope out;
    visitField(list, registry, field, [&]<typename FieldT>{
        if constexpr (IsRefContainer<FieldT> || IsOwnedListField<FieldT>)
        {
            out.isContainer = true;
            using Child = std::remove_cvref_t<typename FieldT::type>;
            if constexpr (requires { typename Child::type; })
                out.registry = registryIdV<typename Child::type>;
        }
    });
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
