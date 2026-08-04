/**
 * @file RegistryEntry.h
 * @brief The registry table: which command sets a given config field.
 *
 * A command and a config field are two views of the same setting. `ip ospf cost`
 * is a @ref cli::tree::CommandNode; @c OspfInterface::COST is an enum value in a
 * config registry. This table is the join between them, so a serializer can walk
 * the config and recover the command that writes each field, and a command can
 * name the field it targets.
 *
 * The mapping is stored as a dense slice per registry. Every registry reserves
 * one @c uint32_t slot for each of its enum values, laid end to end in a single
 * flat array; a registry's slice starts at @c slotOff and runs @c slotCount
 * entries, so the lookup for enum value @c e is a single index:
 *
 *     slots[registry.slotOff + e]
 *
 * Dense rather than sparse because the registries are small and mostly bound —
 * ~1000 slots total across every registry, or about 4 KB. Enum values with no
 * command hold @ref cli::tree::NO_COMMAND.
 *
 * The reverse direction lives on the command itself: @ref cli::tree::CommandNode
 * packs the registry id and the enum index into its @c configId.
 */

#ifndef GRAMMAR_REGISTRY_ENTRY_H
#define GRAMMAR_REGISTRY_ENTRY_H

#include <cstdint>

namespace cli::tree
{
/// @brief Slot value for an enum index no command binds to.
inline constexpr uint32_t NO_COMMAND = 0xFFFFFFFFu;

/**
 * @brief One row per config registry, describing its slice of the slot table.
 *
 * Rows are indexed by registry id, which is the registry's position in
 * REGISTRY_ID_LIST, so the table is dense and a row needs no identity of its
 * own -- the name lives in the source, not the binary. A file written against
 * a different list is caught by @c FileHeader::registryHash.
 */
struct RegistryEntry
{
    uint16_t slotOff;   ///< First slot in the flat slot table belonging to this registry.
    uint16_t slotCount; ///< Slots reserved; equals the registry enum's COUNT.
};

static_assert(sizeof(RegistryEntry) % 4 == 0, "RegistryEntry must keep the slot table aligned");

/**
 * Every slot lives in one flat array indexed by uint16 offsets, so the whole
 * table is capped. Well clear of it today -- around 900 slots with every
 * registry listed -- but the generator must not silently wrap past it.
 */
inline constexpr uint32_t MAX_SLOTS = UINT16_MAX;
}

#endif // GRAMMAR_REGISTRY_ENTRY_H
