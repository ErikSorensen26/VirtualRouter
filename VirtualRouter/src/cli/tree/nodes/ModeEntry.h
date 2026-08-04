/**
 * @file ModeEntry.h
 * @brief The mode table: which command list a session sees from where it stands.
 *
 * Every CLI mode owns a command list, and a session in `(config-if)#` may only
 * run what that mode offers. @ref cli::tree::ModeEntryNode is one row of that
 * lookup — a mode name, an optional submode name, and the command list they map
 * to — and @ref cli::tree::ModeEntry is the cursor over it, mirroring the way
 * @ref cli::tree::Command wraps a @ref cli::tree::CommandNode.
 *
 * Submodes exist because one prompt can mean different grammars: `(config-if)#`
 * on an ethernet port offers commands a tunnel does not. They never nest deeper
 * than one level, which is what keeps this a flat table rather than a tree —
 * a lookup is a mode name plus at most one submode name, never a path.
 *
 * The table sits between the file header and the command nodes in the mapping,
 * so @ref cli::tree::ModeEntryNode is padded to keep the @ref cli::tree::CommandNode
 * array behind it aligned.
 *
 * Cursors borrow from the tree and must not outlive it.
 */

#ifndef GRAMMAR_MODE_ENTRY_H
#define GRAMMAR_MODE_ENTRY_H

#include <cstdint>
#include <string_view>

namespace cli::tree
{
class CommandTree;
class Command;

/**
 * @brief One entry per command list, covering modes and submodes alike.
 *
 * The mode name stays separate from the submode name, and submodes go exactly
 * one level deep, so the table is flat.
 *
 *   (config)#                          mode="(config)#"           sub=""
 *   (config-if)# / ethernet            mode="(config-if)#"        sub="ethernet"
 *   (config-router-af)# / eigrp        mode="(config-router-af)#" sub="eigrp"
 *
 * The mode name and the submode name are interned back to back, so the submode
 * starts at infoOff + modeSiz and needs no offset of its own.
 */
struct ModeEntryNode
{
    /// @brief Registry id on an entry that names no registry.
    static constexpr uint16_t REGISTRY_NONE = 0xFFFFu;

    /**
     * Blob offset of the mode name, followed by the submode name and then the
     * prompt. All three are interned back to back, so each starts where the
     * previous ended and only the first needs an offset.
     */
    uint32_t infoOff;
    uint16_t modeSiz;  ///< Mode name length in bytes.
    uint16_t subSiz;   ///< Submode name length in bytes; 0 for a plain mode.
    uint32_t cmdOff;   ///< Synthetic root CommandNode holding the command list.
    uint16_t cmdSiz;   ///< Commands under that root.

    /**
     * Prompt length in bytes; 0 when the mode declares none.
     *
     * The prompt is data, not identity: several modes share "(config-router)#"
     * and are told apart by their names. Storing it here is what lets a session
     * render its prompt from the tree rather than from a parallel enum table.
     */
    uint16_t promptSiz{0};

    /**
     * The registry a session in this mode writes to, or REGISTRY_NONE.
     *
     * Sits where the alignment padding used to, so it costs nothing. The submode
     * split already separates grammars that share a prompt -- (config-router-af)#
     * is six different command lists -- and that split is the same one that
     * decides which registry is being configured, so the registry belongs on the
     * entry rather than in a table keyed by prompt text.
     */
    uint16_t registryId{REGISTRY_NONE};
};

static_assert(sizeof(ModeEntryNode) % 4 == 0, "ModeEntryNode must keep CommandNode aligned");

/**
 * @brief A cursor over one @ref ModeEntryNode.
 *
 * Mirrors how @ref Command wraps a @ref CommandNode: a tree pointer plus an
 * index, cheap to copy and owning nothing. Names come back as views into the
 * mapped blob, so they stay valid only as long as the tree does.
 */
class ModeEntry
{
public:
    ModeEntry() = default;

    /// @brief The underlying record, for callers that read its fields directly.
    const ModeEntryNode& node() const;

    /// @brief The mode name, e.g. "(config-router-af)#".
    std::string_view name() const;

    /// @brief The submode name, e.g. "ethernet"; empty for a plain mode.
    std::string_view subName() const;

    /// @brief The prompt this mode displays, e.g. "(config-router)#"; empty when unset.
    std::string_view prompt() const;

    /// @brief True when this entry sits under a submode.
    bool hasSubMode() const;

    /// @brief True when this mode names the registry it configures.
    bool hasRegistry() const;

    /**
     * @brief Registry id this mode configures; meaningless unless hasRegistry().
     *
     * Compare against config::registryIdV<ENUM> to recover the type.
     */
    uint16_t registryId() const;

    /// @brief Number of commands in this entry's list.
    size_t size() const;

    /**
     * @brief The command list, as a cursor on its synthetic root.
     *
     * The list is stored as the children of a root @ref CommandNode that has no
     * name of its own, so iterating the returned @ref Command yields exactly the
     * commands this mode offers.
     */
    Command commands() const;

private:
    friend class CommandTree;
    ModeEntry(const CommandTree* tree, uint32_t index);

    const CommandTree* tree{nullptr}; ///< Owning tree; null in a default-constructed cursor.
    uint32_t index{0};                ///< Row in the tree's mode table.
};
}

#endif // GRAMMAR_MODE_ENTRY_H
