/**
 * @file Command.h
 * @brief A node in the flattened command tree, and a cursor over it.
 *
 * @c CommandNode is the packed on-disk record: ids into the string tables for
 * its name and description, the span of its children, and its property flags.
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
/**
 * @brief Widest sibling set a per-line "already used" mask can track.
 *
 * Bounds the recursive property: it remembers which siblings a line has already
 * taken, and the mask holding that is one word wide.
 */
constexpr size_t MAX_TRACKED_SIBLINGS = 64;

class CommandTree;

struct CommandNode
{
    enum Property : uint32_t
    {
        NEGATE            = 1u << 0,
        NEGATE_ALL        = 1u << 1,
        NEGATE_HIDE       = 1u << 2,
        NEGATE_SHOW       = 1u << 3,
        RECURSIVE         = 1u << 4, // On a parent: its children may repeat.
        MULTI_USE         = 1u << 5, // In a recursive set: this child is not spent when used.
        // 1u << 6 free (was SUBCMD_SINGLE_USE, now the default).
        MODE_CHANGE       = 1u << 7, // Entering a mode; configExt names which one.
        TUPLE_CHANGE      = 1u << 8,
        ENUM_CHANGE       = 1u << 9,
        MODE_EXIT         = 1u << 10, // Leaving one mode, as `exit` does.
        ENUM_BITMAP       = 1u << 11, // Qualifies ENUM_CHANGE; the field is a bitmap.
        REGISTRY_CHANGE   = 1u << 12, // Binds a container: rescopes the line, reverts after.
        DEFERRED          = 1u << 13, // Holds its value under a key; configExt names which.
        RESOLVER          = 1u << 14, // Supplies the value for a key; configExt names which.
        TUPLE_ENUM        = 1u << 15, // Qualifies TUPLE_CHANGE; configExt is split, see tupleEnumIndex().
        RECURSE_EXCLUDE_ALL = 1u << 16, // In a repeat set: using this member ends the set outright.
        RECURSE_HIDE        = 1u << 17, // In a repeat set: absent from the re-offer once a sibling is used.
        RECURSE_SHOW_ALL    = 1u << 18, // In a repeat set: using this member waives RECURSE_HIDE for the rest of the line.
        RECURSE_EXCLUDE     = 1u << 19, // In a repeat set: using this member drops the named siblings in recurseExcludeId.
    };

    /**
     * @brief A tuple member that is an enum needs two numbers where every other node
     * needs one: which member of the tuple, and which member of the enum. Both
     * live in configExt, eight bits each.
     *
     * Still a cap, so both halves stay range-checked when they are packed.
     * Silently truncating a member or an enumerator would write the wrong value
     * at runtime with nothing to trace it back to, which no amount of headroom
     * makes safe.
     */
    static constexpr uint16_t TUPLE_ENUM_BITS = 8;
    static constexpr uint16_t TUPLE_ENUM_MASK = (1u << TUPLE_ENUM_BITS) - 1;
    static constexpr uint16_t TUPLE_ENUM_MAX  = TUPLE_ENUM_MASK;

    static constexpr uint16_t packTupleEnum(uint16_t member, uint16_t enumIdx)
    {
        return static_cast<uint16_t>((enumIdx << TUPLE_ENUM_BITS) | member);
    }

    /**
     * @brief configId packs the config field this command writes into 32 bits: the
     * high 12 bits are the registry id, the low 20 bits the enum index within
     * that registry.
     *
     * CONFIG_NONE marks a command that sets no config field, which is most of
     * them -- containers like "ip" or "router" only exist to be descended
     * through. It reserves the top registry id, so ids run 0..4094.
     */
    static constexpr uint32_t CONFIG_FIELD_ENUM_BITS = 20;
    static constexpr uint32_t CONFIG_FIELD_ENUM_MASK = (1u << CONFIG_FIELD_ENUM_BITS) - 1;
    static constexpr uint32_t CONFIG_FIELD_MAX_REGISTRY = (1u << (32 - CONFIG_FIELD_ENUM_BITS)) - 2;

    static constexpr uint32_t CONFIG_NONE = 0xFFFFFFFFu;
    static constexpr uint16_t CONFIG_EXT_NONE = 0xFFFFu;

    /// @brief Id of an interned string that is absent rather than empty.
   static constexpr uint16_t STR_NONE = 0xFFFFu;

    /// @brief Value of parentIndex for a node with no parent: a mode's synthetic root.
    static constexpr uint32_t NODE_NONE = 0xFFFFFFFFu;

    uint32_t subcmdOff;               ///< Index of the first child node.
    uint32_t configId = CONFIG_NONE;  ///< Packed registry id and field index.
    uint32_t flags = 0;               ///< Property bits.
    uint16_t nameId = STR_NONE;       ///< Interned name; STR_NONE when unnamed.
    uint16_t descId = STR_NONE;       ///< Interned description; STR_NONE when absent.
    uint16_t subcmdSiz = 0;           ///< Children under subcmdOff.
    uint16_t configExt = CONFIG_EXT_NONE;

    /**
     * @brief Id of the deferral key this node holds under or resolves.
     *
     * Split out of configExt so DEFERRED and RESOLVER no longer contend with
     * ENUM_CHANGE, TUPLE_CHANGE or MODE_CHANGE for the same byte -- a node can
     * now defer a key and also set an enum member, or enter a mode, in one
     * command. Carved from the reserved words rather than widening the
     * struct; see command-node-spare-flag-bits.
     */
    uint16_t deferKeyId = CONFIG_EXT_NONE;

    /**
     * @brief Id of the interned CSV of sibling names this member excludes when used.
     *
     * Only meaningful alongside RECURSE_EXCLUDE. Unlike RECURSE_EXCLUDE_ALL, which
     * ends the whole repeat set, this drops only the named siblings from the
     * re-offer -- the rest of the set stays available. Stored as one interned
     * string rather than a set of ids so the payload rides the existing string
     * blob instead of a new table; split and matched against sibling names by
     * @ref hasRecurseExclude callers at traversal time.
     */
    uint16_t recurseExcludeId = STR_NONE;

    uint32_t parentIndex = NODE_NONE; ///< Index of the node this one is a child of; NODE_NONE for a mode's root.
    uint32_t reserved2 = 0;

    bool has(Property o) const { return flags & o; }

    /**
     * @brief True when this node has a parent; false only for a mode's synthetic root.
     */
    bool hasParent() const { return parentIndex != NODE_NONE; }

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
     * @brief True when the enum member sets a bit rather than replacing a value.
     *
     * Only meaningful alongside hasEnumChange(). A bitmap field holds flags, so
     * sibling commands on one line accumulate -- `eigrp stub connected summary`
     * sets two bits of the one field, where a value field would keep the last.
     */
    bool hasEnumBitMap() const { return flags & ENUM_BITMAP; }

    /**
     * @brief True when this command rescopes the rest of its own line.
     *
     * A mode change moves the session; this moves only the write scope, and
     * only until the line ends -- `ip dhcp pool LAN dns-server 8.8.8.8` edits
     * the pool without leaving the mode the user is standing in.
     *
     * Implies hasConfig(), like a mode change does and for the same reason: the
     * bound field is where the new registry comes from. It needs no configExt,
     * since it names no mode -- the scope is the field itself.
     */
    bool hasRegistryChange() const { return flags & REGISTRY_CHANGE; }

    /**
     * @brief True when this command writes one member of a tuple-valued field.
     *
     * configExt holds the member's position in the tuple, which is a std::get
     * index. Several such commands on one line name members of the same entry,
     * so the executor stages them and inserts once rather than writing each.
     */
    bool hasTuple() const { return (flags & TUPLE_CHANGE) && configExt != CONFIG_EXT_NONE; }

    /**
     * @brief True when this tuple member is set to a named enum member.
     *
     * The tuple counterpart of hasEnumChange(). A keyword names the member and
     * the value both -- `in` says which member of DistributeList and that it is
     * IN -- so the node carries no token and the value is fixed at flatten time.
     */
    bool hasTupleEnum() const { return hasTuple() && (flags & TUPLE_ENUM); }

    /**
     * @brief The std::get index this command writes, in either tuple mode.
     */
    uint16_t tupleMember() const
    {
        return (flags & TUPLE_ENUM) ? static_cast<uint16_t>(configExt & TUPLE_ENUM_MASK)
                                    : configExt;
    }

    /**
     * @brief The enum member this tuple member is set to; only with hasTupleEnum().
     */
    uint16_t tupleEnumIndex() const
    {
        return static_cast<uint16_t>(configExt >> TUPLE_ENUM_BITS);
    }

    /**
     * @brief True when this command's value is held under a key rather than written.
     *
     * deferKeyId holds the key's id, not the key itself: the flattener numbers
     * the names it meets across the whole grammar and stores the position, the
     * way a mode change stores a CLI_MODE_TABLE index. Two commands naming one
     * key therefore carry the same id, and the name itself does not reach the
     * binary. It lives apart from configExt, so a deferral no longer contends
     * with an enum, tuple or mode binding for the same byte -- a node may defer
     * a key and also set an enum member in one command.
     *
     * Independent of hasConfig(), like hasResolver() is: a node that names a
     * field resolves into it once the key resolves; one that names none
     * instead inherits the field from whichever resolver its key pairs with --
     * needed when a shared deferred value's resolvers write different fields
     * (e.g. filter-list's WORD, resolved by either `in` or `out`). Until then
     * the eventual field keeps its declared default.
     */
    bool hasDeferred() const { return (flags & DEFERRED) && deferKeyId != CONFIG_EXT_NONE; }

    /**
     * @brief True when this command supplies the value a deferred key waits on.
     *
     * The other half of hasDeferred(), numbered out of the same table so the ids
     * match. What it resolves is whatever deferred commands named the same key,
     * wherever in the grammar they sit.
     *
     * Whether it also writes somewhere is hasConfig()'s business, and the two
     * are independent. A resolver that binds no field exists only to supply a
     * value, so execution leaves it out of the walk and reads it by key; one
     * that binds a field is a command as well, and is walked in its place.
     */
    bool hasResolver() const { return (flags & RESOLVER) && deferKeyId != CONFIG_EXT_NONE; }

    /**
     * @brief The key id this command defers under or resolves; only with either flag.
     */
    uint16_t deferKey() const { return deferKeyId; }

    /**
     * @brief True when using this member drops specific named siblings from the
     * repeat set's re-offer, rather than ending the set outright.
     */
    bool hasRecurseExclude() const { return (flags & RECURSE_EXCLUDE) && recurseExcludeId != STR_NONE; }

    /**
     * @brief Registry id of the bound field; meaningless unless hasConfig().
     */
    uint16_t fieldRegistryId() const { return static_cast<uint16_t>(configId >> CONFIG_FIELD_ENUM_BITS); }

    /**
     * @brief Enum index within that registry; meaningless unless hasConfig().
     */
    uint32_t enumIndex() const { return configId & CONFIG_FIELD_ENUM_MASK; }

    /**
     * @brief Builds a packed configId from a registry id and enum index.
     *
     * Both halves are asserted rather than masked. A registry id of 63 packs to
     * CONFIG_NONE and an oversized index bleeds into the registry bits, so
     * either would decode as a different binding than the one requested --
     * silently, and only for grammars large enough to reach the limit.
     */
    static constexpr uint32_t packConfig(uint16_t registry, uint32_t index)
    {
        return (static_cast<uint32_t>(registry) << CONFIG_FIELD_ENUM_BITS) | index;
    }
};

static_assert(sizeof(CommandNode) == 32, "CommandNode layout is the on-disk format");
static_assert(alignof(CommandNode) == 4, "CommandNode must stay 4-byte aligned in the mapping");

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

    /// @brief True when this node carries a property flag; false when unbound.
    bool hasProp(CommandNode::Property) const;

    /// @brief True when "<cr>" is this node's only child, so the command ends here.
    bool hasExclusiveCR() const;

    /// @brief True when "<cr>" is among this node's children, so it may end here.
    bool hasCarriageReturn() const;

    /// @brief The keyword this node matches; "<cr>" for a default cursor.
    std::string_view name() const;

    /// @brief The help text shown beside the name; empty when none was written.
    std::string_view desc() const;

    /// @brief The raw "member1,member2,..." payload of a `recurse_exclude`; empty when absent.
    std::string_view recurseExcludeCsv() const;

    /// @brief Number of children; zero for a leaf or a default cursor.
    size_t size() const;

    /// @brief A cursor on the nth child; invalid when @p i is out of range.
    Command at(size_t i) const;

    /// @brief A cursor on this node's parent; invalid at a mode's synthetic root.
    Command parent() const;

    /// @brief Ordinal of the child named @p childName, or NPOS when absent.
    size_t find(std::string_view childName) const;

    /// @brief Returned by @ref find for a name that is not a child.
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
    const CommandTree* tree = nullptr;
    uint32_t index = 0;
};
}

#endif // GRAMMAR_COMMAND_H
