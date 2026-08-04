/**
 * @file Command.h
 * @brief A node in the flattened command tree, and a cursor over it.
 *
 * @c CommandNode is the packed on-disk record: offsets and lengths into the
 * string blob for its name and description, the span of its children, and its
 * property flags.
 *
 * @c Command is the cursor callers hold — a tree pointer plus an index, cheap to
 * copy, owning nothing. Names and descriptions come back as @c string_view
 * pointing into the mapped blob, so traversal allocates nothing and stays valid
 * only as long as the tree does.
 *
 * A default-constructed @c Command is the @c <cr> leaf: it has a null tree,
 * reports @c "<cr>" as its name, and has no children. Help output pushes one to
 * mark a point where the command may end.
 */

#ifndef GRAMMAR_COMMAND_H
#define GRAMMAR_COMMAND_H

#include <cstdint>
#include <string_view>

namespace cli::tree
{
// Keys as they appear in a grammar file.
/// "prompt" or "prompt/variant" on a command that enters a mode.
constexpr std::string_view KEY_MODE        = "mode";
constexpr std::string_view KEY_NAME        = "name";
constexpr std::string_view KEY_DESCRIPTION = "description";
constexpr std::string_view KEY_SUBCOMMANDS = "subcommands";
constexpr std::string_view KEY_PROPERTIES  = "properties";
constexpr std::string_view KEY_SUPPORT     = "support";
constexpr std::string_view KEY_CONFIG      = "config";
constexpr std::string_view KEY_ENUM        = "enum";
constexpr std::string_view KEY_ARGS        = "args";

/// @brief Marks a value that names an argument rather than being one.
constexpr char ARG_SIGIL = '$';

constexpr std::string_view KEY_VARIABLES   = "VARIABLES";

/**
 * @brief Every key a command object may carry.
 *
 * Checked against, so a key the flattener does not know is an error rather than
 * something silently dropped. Six misspellings were already in the grammar when
 * this went in -- "descirption", "subcommads" -- and a mistyped "subcommands"
 * costs a whole subtree with nothing to show that it went missing.
 */
constexpr std::string_view COMMAND_KEYS[] = {
    KEY_NAME, KEY_DESCRIPTION, KEY_SUBCOMMANDS, KEY_PROPERTIES,
    KEY_SUPPORT, KEY_CONFIG, KEY_ENUM, KEY_MODE, KEY_ARGS,
};

// Keys of a per-mode grammar file. Each file is one mode: its prompt, the
// registry it configures, and its command list.
constexpr std::string_view KEY_PROMPT      = "prompt";
constexpr std::string_view KEY_REGISTRY    = "registry";
constexpr std::string_view KEY_COMMANDS    = "commands";
constexpr std::string_view KEY_VARIANT     = "variant";

/// @brief Basename, without extension, of the shared variable file.
constexpr std::string_view VARIABLES_STEM  = "Variables";

constexpr size_t MAX_TRACKED_SIBLINGS = 64;

class CommandTree;

struct CommandNode
{
    enum Property : uint16_t
    {
        NEGATE            = 1u << 0,
        NEGATE_ALL        = 1u << 1,
        NEGATE_HIDE       = 1u << 2,
        NEGATE_SHOW       = 1u << 3,
        RECURSIVE         = 1u << 4, // Non repeatable commands
        SUBCMD_SEQUENCE   = 1u << 5,
        SUBCMD_SINGLE_USE = 1u << 6,
        MODE_CHANGE       = 1u << 7, // Entering a mode; configExt names which one.
        TUPLE_CHANGE      = 1u << 8,
        ENUM_CHANGE       = 1u << 9,
        MODE_EXIT         = 1u << 10, // Leaving one mode, as `exit` does.
    };

    /**
     * configId packs the config field this command writes into 16 bits: the
     * high 6 bits are the registry id, the low 10 bits the enum index within
     * that registry. Kept packed so CommandNode stays 16 bytes.
     *
     * CONFIG_NONE marks a command that sets no config field, which is most of
     * them -- containers like "ip" or "router" only exist to be descended
     * through. It reserves registry id 63, so ids run 0..62.
     */
    static constexpr uint16_t CONFIG_FIELD_ENUM_BITS = 10;
    static constexpr uint16_t CONFIG_FIELD_ENUM_MASK = (1u << CONFIG_FIELD_ENUM_BITS) - 1;
    static constexpr uint16_t CONFIG_FIELD_MAX_REGISTRY = (1u << (16 - CONFIG_FIELD_ENUM_BITS)) - 2;

    static constexpr uint16_t CONFIG_NONE = 0xFFFFu;
    static constexpr uint16_t CONFIG_EXT_NONE = 0xFFu;

    uint32_t infoOff;
    uint32_t subcmdOff;
    uint16_t configId = CONFIG_NONE;
    uint8_t  configExt = CONFIG_EXT_NONE;
    uint8_t  nameSiz;
    uint8_t  descSiz;
    uint8_t  subcmdSiz;
    uint16_t flags;

    bool has(Property o) const { return flags & o; }

    /**
     * @brief True when this command writes a config field.
     */
    bool hasConfig() const { return configId != CONFIG_NONE; }

    /**
     * @brief True when running this command enters a mode.
     *
     * Independent of hasConfig(): a mode change binds a field as well, and needs
     * it -- the bound container is where the mode's registry comes from.
     */
    bool hasModeChange() const { return (flags & MODE_CHANGE) && configExt != CONFIG_EXT_NONE; }

    /**
     * @brief True when running this command leaves the current mode.
     *
     * Unlike hasModeChange() this reads the flag alone. An exit names no mode --
     * where it lands is whatever the navigation stack was holding -- so it binds
     * no field and leaves configExt unclaimed.
     */
    bool hasModeExit() const { return flags & MODE_EXIT; }

    /**
     * @brief True when this command sets its field to a named enum member.
     *
     * configExt holds the member's own enum value, resolved by the flattener, so
     * running the command is a write rather than a second lookup. Implies
     * hasConfig(): the field is what the member's type was checked against.
     */
    bool hasEnumChange() const { return (flags & ENUM_CHANGE) && configExt != CONFIG_EXT_NONE; }

    /**
     * @brief True when this command writes one member of a tuple-valued field.
     *
     * configExt holds the member's position in the tuple, which is a std::get
     * index. Several such commands on one line name members of the same entry,
     * so the executor stages them and inserts once rather than writing each.
     */
    bool hasTuple() const { return (flags & TUPLE_CHANGE) && configExt != CONFIG_EXT_NONE; }

    /**
     * @brief Registry id of the bound field; meaningless unless hasConfig().
     */
    uint16_t fieldRegistryId() const { return configId >> CONFIG_FIELD_ENUM_BITS; }

    /**
     * @brief Enum index within that registry; meaningless unless hasConfig().
     */
    uint16_t enumIndex() const { return configId & CONFIG_FIELD_ENUM_MASK; }

    /**
     * @brief Builds a packed configId from a registry id and enum index.
     *
     * Both halves are asserted rather than masked. A registry id of 63 packs to
     * CONFIG_NONE and an oversized index bleeds into the registry bits, so
     * either would decode as a different binding than the one requested --
     * silently, and only for grammars large enough to reach the limit.
     */
    static constexpr uint16_t packConfig(uint16_t registry, uint16_t index)
    {
        return static_cast<uint16_t>((registry << CONFIG_FIELD_ENUM_BITS) | index);
    }
};

class Command
{
public:
    Command() = default;

    /// @brief False for a default constructed cursor, e.g. an unbound config field.
    bool valid() const { return tree != nullptr; }
    explicit operator bool() const { return valid(); }

    Command(const Command&) = default;
    Command& operator=(const Command&) = default;

    /// @brief The underlying node, for callers that read flags directly.
    const CommandNode& node() const { return resolveNode(); }

    /**
     * @brief This cursor's node index, for callers storing a node compactly.
     *
     * A @ref Token keeps one of these rather than a whole cursor: every token on
     * a line resolves against the same tree, so the tree pointer would be the
     * same eight bytes repeated. Pair it back with the tree via @ref rebind.
     */
    uint32_t nodeIndex() const { return index; }

    /**
     * @brief Rebuilds a cursor from an index this tree produced.
     *
     * The inverse of @ref nodeIndex. Only meaningful for an index that came from
     * a cursor over @p tr; an index from another tree resolves to whatever node
     * happens to sit at that offset.
     */
    static Command rebind(const CommandTree* tr, uint32_t idx) { return Command(tr, idx); }

    bool hasProp(CommandNode::Property) const;

    bool hasExclusiveCR() const;
    bool hasCarriageReturn() const;

    std::string_view name() const;

    std::string_view desc() const;

    size_t size() const;

    Command at(size_t i) const;

    size_t find(std::string_view childName) const;

    static constexpr size_t NPOS = ~size_t{0};

    class Iterator
    {
    public:
        Iterator(const Command* parent, uint32_t i) : parent(parent), i(i) {}
        Command operator*() const { return parent->at(i); }
        Iterator& operator++() { ++i; return *this; }
        bool operator!=(const Iterator& o) const { return i != o.i; }
    private:
        const Command* parent;
        uint32_t i;
    };

    Iterator begin() const { return Iterator(this, 0); }
    Iterator end()   const { return Iterator(this, static_cast<uint32_t>(size())); }

private:
    friend class CommandTree;
    friend class ModeEntry;
    Command(const CommandTree* tree, uint32_t index);

    const CommandNode& resolveNode() const;

protected:
    const CommandTree* tree;
    uint32_t index;
};
}

#endif // GRAMMAR_COMMAND_H