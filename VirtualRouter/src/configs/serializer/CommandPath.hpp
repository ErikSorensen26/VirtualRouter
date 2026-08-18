/**
 * @file CommandPath.hpp
 * @brief Maps a config field back to the canonical keyword path that writes it.
 * @ingroup CONFIG_SERIALIZER
 *
 * Execution goes tree -> field: a matched @c CommandNode carries a configId and
 * writes whatever it names. Serialization needs the other direction -- a field
 * the caller already has, and the words that would set it -- which is not a
 * question the grammar tree answers on its own, since several nodes can bind
 * the same configId (aliases, `no` forms, a value form and an enum form).
 *
 * @c CommandPathIndex resolves that ambiguity once, at load time, rather than
 * on every field printed: it walks every mode's command list and keeps the
 * first node it meets for each key, so the same field always serializes to the
 * same words regardless of which phrasing a session actually used to set it.
 * "First seen" is a deliberate, cheap tie-break -- see the class doc below --
 * not a claim that it is the best of the candidates.
 *
 * A key is a config field, optionally qualified by which tuple member or enum
 * member it names; see @ref CommandKey.
 */

#if 0

#ifndef CONFIG_SERIALIZER_COMMAND_PATH_HPP
#define CONFIG_SERIALIZER_COMMAND_PATH_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cli/tree/CommandTree.h"
#include "cli/tree/nodes/Command.h"
#include "cli/tree/nodes/ModeEntry.h"

namespace config::serializer
{
/**
 * @brief Identifies what a @c CommandNode binds, at the granularity serialization needs.
 *
 * @c configId alone is enough for a plain value field. A tuple member (an OSPF
 * area range's `cost`) additionally needs which member, and an enum-valued
 * field additionally needs which member is being named -- two different
 * command nodes both bind AREA_TYPE, one per @c AreaType member, and each has
 * to resolve to its own node rather than to whichever the index saw first.
 *
 * @c CONFIG_EXT_NONE in either slot means "not applicable", matching
 * @c CommandNode::CONFIG_EXT_NONE, so a plain field's key does not collide
 * with a tuple or enum field that happens to share configId's low bits.
 */
struct CommandKey
{
    uint32_t configId = ~0u;
    uint16_t tupleMember = 0xFFFFu; ///< CommandNode::CONFIG_EXT_NONE when not a tuple member.
    uint16_t enumMember  = 0xFFFFu; ///< CommandNode::CONFIG_EXT_NONE when not an enum-change node.

    bool operator==(const CommandKey&) const = default;
};

struct CommandKeyHash
{
    size_t operator()(const CommandKey& k) const noexcept
    {
        size_t h = std::hash<uint32_t>{}(k.configId);
        h = h * 0x100000001B3ull ^ std::hash<uint16_t>{}(k.tupleMember);
        h = h * 0x100000001B3ull ^ std::hash<uint16_t>{}(k.enumMember);
        return h;
    }
};

/**
 * @brief The reconstructed words for one command, from a mode's root to the leaf.
 *
 * Plain keywords and the fixed words of a two-token value ("cost", the enum
 * member name) all live in @c words. A field that still needs a value token
 * appended -- most scalar fields -- leaves that to the caller, since the index
 * has no value to print; only the node it found does.
 */
struct CommandWords
{
    std::vector<std::string_view> words;
    cli::tree::Command node; ///< The resolved node itself, for callers that need its flags.
};

/**
 * @brief Reverse index from a config field to the node that names it canonically.
 *
 * Built once from a @ref cli::tree::CommandTree and kept for the process
 * lifetime; nothing here mutates after @ref build returns. Cursors borrowed
 * from the tree stay valid exactly as long as the tree does, same as every
 * other @c Command in this codebase.
 *
 * ## Tie-break
 * Several nodes can bind one field -- a value form and a `no` form toggling
 * the same bool, or one enum-change node per member of an enum-valued field.
 * The enum case needs every member kept, since each is a different value; the
 * others are genuine duplicates of one one field; the first one the walk
 * meets wins and the rest are ignored. Modes are walked in table order and a
 * mode's commands depth-first in child order, so "first" is deterministic
 * across builds of the same grammar, but is a stable choice rather than a
 * best one -- a grammar wanting a *specific* spelling preferred should list
 * it first under its mode rather than rely on this.
 */
class CommandPathIndex
{
public:
    CommandPathIndex() = default;

    /// @brief Walks every mode in @p tree and indexes every config-bearing node.
    explicit CommandPathIndex(const cli::tree::CommandTree& tree)
    {
        build(tree);
    }

    void build(const cli::tree::CommandTree& tree)
    {
        table.clear();
        sourceTree = &tree;
        for (size_t m = 0; m < tree.modeCount(); ++m)
        {
            cli::tree::ModeEntry mode = tree.modeEntry(m);
            cli::tree::Command root = mode.commands();
            walk(root);
        }
    }

    /// @brief The canonical path for a plain (non-tuple, non-enum) field, or empty words with an invalid node.
    CommandWords lookup(uint32_t configId) const
    {
        return lookupKey(CommandKey{configId, 0xFFFFu, 0xFFFFu});
    }

    /// @brief The canonical path for one member of a tuple-valued field.
    CommandWords lookupTupleMember(uint32_t configId, uint16_t member) const
    {
        return lookupKey(CommandKey{configId, member, 0xFFFFu});
    }

    /// @brief The canonical path for one member of an enum-valued field.
    CommandWords lookupEnumMember(uint32_t configId, uint16_t member) const
    {
        return lookupKey(CommandKey{configId, 0xFFFFu, member});
    }

private:
    CommandWords lookupKey(const CommandKey& key) const
    {
        auto it = table.find(key);
        if (it == table.end()) return {};
        return buildWords(it->second);
    }

    /// @brief Depth-first walk indexing every node that binds a field, keyword or value alike.
    void walk(cli::tree::Command cmd)
    {
        const cli::tree::CommandNode& n = cmd.node();

        if (n.hasConfig())
        {
            if (n.hasEnumChange())
                table.try_emplace(CommandKey{n.configId, 0xFFFFu, n.configExt}, cmd.nodeIndex());
            else if (n.hasTuple() && n.hasTupleEnum())
                table.try_emplace(CommandKey{n.configId, n.tupleMember(), n.tupleEnumIndex()},
                                   cmd.nodeIndex());
            else if (n.hasTuple())
                table.try_emplace(CommandKey{n.configId, n.tupleMember(), 0xFFFFu}, cmd.nodeIndex());
            else
                table.try_emplace(CommandKey{n.configId, 0xFFFFu, 0xFFFFu}, cmd.nodeIndex());
        }

        for (cli::tree::Command child : cmd)
            walk(child);
    }

    /// @brief Walks a resolved node back to its mode's root, then reverses the collected names.
    CommandWords buildWords(uint32_t nodeIndex) const
    {
        CommandWords out;
        out.node = cli::tree::Command::rebind(sourceTree, nodeIndex);

        std::vector<std::string_view> reversed;
        for (cli::tree::Command c = out.node; c; c = c.parent())
        {
            std::string_view name = c.name();
            if (!name.empty() && name != "<cr>")
                reversed.push_back(name);
        }

        out.words.assign(reversed.rbegin(), reversed.rend());
        return out;
    }

    const cli::tree::CommandTree* sourceTree = nullptr;
    std::unordered_map<CommandKey, uint32_t, CommandKeyHash> table;
};
}

#endif // CONFIG_SERIALIZER_COMMAND_PATH_HPP
#endif
