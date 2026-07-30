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
// Keys as they appear in Commands.json.
constexpr std::string_view KEY_NAME        = "name";
constexpr std::string_view KEY_DESCRIPTION = "description";
constexpr std::string_view KEY_SUBCOMMANDS = "subcommands";
constexpr std::string_view KEY_PROPERTIES  = "properties";
constexpr std::string_view KEY_SUPPORT     = "support";

constexpr std::string_view KEY_VARIABLES   = "VARIABLES";
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
        SUPPORT           = 1u << 7, // The command is supported
        SUPPORT_SET       = 1u << 8, // "support" key was present in the grammar
    };

    uint32_t infoOff;
    uint32_t subcmdOff;
    uint16_t flags;
    uint8_t nameSiz;
    uint8_t padd{0};
    uint16_t descSiz;
    uint16_t subcmdSiz;

    bool has(Property o) const { return flags & o; }

    /// Absent "support" means supported; only an explicit false marks it otherwise.
    bool supported() const { return (flags & SUPPORT); }
};

class Command
{
public:
    Command() = default;

    Command(const Command&) = default;
    Command& operator=(const Command&) = default;

    /// @brief The underlying node, for callers that read flags directly.
    const CommandNode& node() const { return resolveNode(); }

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
