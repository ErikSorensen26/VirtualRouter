// TreeParser.cpp

// TODO add "recurse_exclude_all", "recurse_show_all" (overrides hide) and "recurse_hide"

#include <utils/Json.hpp>
#include "CommandTree.h"
#include "nodes/Command.h"
#include "nodes/GrammarKeys.h"
#include "nodes/FileHeader.hpp"
#include "configs/RegistryTable.hpp"
#include "cli/modes/Mode.hpp"
#include <deque>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

namespace cli::tree::parser
{
namespace
{
using utils::json::JsonNode;

const JsonNode* member(const JsonNode& obj, std::string_view key)
{
    for (const JsonNode& c : obj.children)
        if (c.name == key) return &c;
    return nullptr;
}

// A misspelled key is not inert: "subcommads" silently costs the whole subtree.
void rejectUnknownKeys(const JsonNode& obj)
{
    for (const JsonNode& c : obj.children)
    {
        bool known = false;
        for (std::string_view k : COMMAND_KEYS)
            if (c.name == k) { known = true; break; }

        if (!known)
            throw std::runtime_error("cli::grammar: unknown key '" + c.name
                + "' on command object" + (member(obj, KEY_NAME)
                    ? " '" + member(obj, KEY_NAME)->strValue + "'" : ""));
    }
}

bool isCommandArray(const JsonNode& n)
{
    return n.type == JsonNode::ARRAY
        && !n.children.empty()
        && n.children.front().type == JsonNode::OBJECT
        && member(n.children.front(), KEY_NAME) != nullptr;
}

// Unwraps [ { "eigrp-ipv4": [...] } ]; modes sharing a prompt differ by variant.
const JsonNode& variantModeDict(const JsonNode& n, const std::string& who)
{
    if (n.type != JsonNode::ARRAY || n.children.size() != 1
        || n.children.front().type != JsonNode::OBJECT)
        throw std::runtime_error("cli::grammar: '" + who
            + "' is neither a command array nor a single-object variant list");
    return n.children.front();
}

// Resolves "prompt" or "prompt/variant" against CLI_MODE_TABLE, not the mode files.
uint16_t resolveMode(std::string_view spec, const std::string& who)
{
    std::string_view prompt = spec;
    std::string_view variant;
    if (size_t sep = spec.find('/'); sep != std::string_view::npos)
    {
        prompt  = spec.substr(0, sep);
        variant = spec.substr(sep + 1);
    }

    size_t i = 0;
#define X(name, ...) \
    { \
        constexpr auto path = cli::makePath(__VA_ARGS__); \
        if (path[0] == prompt \
            && ((path.size() > 1 ? path[1] : std::string_view{}) == variant)) \
            return static_cast<uint16_t>(i); \
        ++i; \
    }
    CLI_MODE_TABLE
#undef X

    throw std::runtime_error("cli::grammar: '" + who + "' names mode '"
        + std::string(spec) + "', which is not in CLI_MODE_TABLE");
}

uint32_t propertyFlag(std::string_view p)
{
    if (p == "negate")               return CommandNode::NEGATE;
    if (p == "negate_all")           return CommandNode::NEGATE_ALL;
    if (p == "negate_hide")          return CommandNode::NEGATE_HIDE;
    if (p == "negate_show")          return CommandNode::NEGATE_SHOW;
    if (p == "recursive")            return CommandNode::RECURSIVE;
    if (p == "multi_use")            return CommandNode::MULTI_USE;
    if (p == "recurse_exclude_all")  return CommandNode::RECURSE_EXCLUDE_ALL;
    if (p == "recurse_hide")         return CommandNode::RECURSE_HIDE;
    if (p == "recurse_show_all")     return CommandNode::RECURSE_SHOW_ALL;
    if (p == "mode_exit")            return CommandNode::MODE_EXIT;
    throw std::runtime_error("cli::grammar: unknown property '" + std::string(p) + "'");
}

// Splits "Registry::field[::member]"; a fourth part stays in member and fails to resolve.
struct ConfigKey
{
    std::string_view registry;
    std::string_view field;   // Empty for a registry root, which names no field.
    std::string_view member;  // Empty unless the key names a tuple member.
};

ConfigKey splitConfigKey(std::string_view key)
{
    size_t sep = key.find("::");
    if (sep == std::string_view::npos)
        return {key, {}, {}};

    std::string_view rest = key.substr(sep + 2);

    size_t sep2 = rest.find("::");
    if (sep2 == std::string_view::npos)
        return {key.substr(0, sep), rest, {}};

    return {key.substr(0, sep), rest.substr(0, sep2), rest.substr(sep2 + 2)};
}

// Throws rather than missing silently: an unresolved name is a typo or a stale rename.
uint16_t resolveRegistry(std::string_view name, const std::string& who)
{
    uint16_t id = config::findRegistry(config::RegistryEntries{},
                                       config::tokenHash(name));
    if (id == config::NOT_FOUND)
        throw std::runtime_error("cli::grammar: '" + who
            + "' names unknown registry '" + std::string(name) + "'");
    return id;
}

uint16_t resolveField(uint16_t registry, std::string_view regName,
                      std::string_view name, const std::string& who)
{
    uint16_t idx = config::findFieldIn(config::RegistryEntries{}, registry,
                                       config::tokenHash(name));
    if (idx == config::NOT_FOUND)
        throw std::runtime_error("cli::grammar: '" + who
            + "' names unknown field '" + std::string(name) + "' in registry '"
            + std::string(regName) + "'");
    return idx;
}

// A mode names a bare registry; a "::" here is a command key that landed on it by mistake.
uint16_t modeRegistryName(const JsonNode& cfg, const std::string& who)
{
    if (cfg.type != JsonNode::STRING)
        throw std::runtime_error("cli::grammar: mode '" + who
            + "' has a non-string registry");

    ConfigKey key = splitConfigKey(cfg.strValue);
    if (!key.field.empty() || !key.member.empty())
        throw std::runtime_error("cli::grammar: mode '" + who
            + "' names field '" + std::string(key.field)
            + "'; a mode names only a registry");

    return resolveRegistry(key.registry, who);
}

// Case-insensitive lookup: "Registry" for "registry" would silently cost a mode its binding.
const JsonNode* memberLoose(const JsonNode& obj, std::string_view key)
{
    if (const JsonNode* exact = member(obj, key)) return exact;

    for (const JsonNode& c : obj.children)
    {
        if (c.name.size() != key.size()) continue;
        bool same = true;
        for (size_t i = 0; i < key.size() && same; ++i)
            same = std::tolower(static_cast<unsigned char>(c.name[i]))
                == std::tolower(static_cast<unsigned char>(key[i]));
        if (same) return &c;
    }
    return nullptr;
}

// One mode grammar file; borrows the parsed document, so it stays freely movable.
struct ModeFile
{
    std::string name;      // The prompt; half of the key getMode looks up.
    std::string variant;   // "variant" key, or empty.
    std::string prompt;
    std::string origin;    // Source filename, for diagnostics only.
    uint16_t registry = ModeEntryNode::REGISTRY_NONE;
    const JsonNode* commands = nullptr;
};

// Checks every registry fits what configId can index; a field's own command is
// recorded in the registry at write time, so nothing is laid out here.
struct RegBuilder
{
    template <typename Entry>
    void operator()()
    {
        using Enum = typename Entry::type;
        constexpr std::size_t count = config::registrySlotsV<Enum>;

        static_assert(count <= CommandNode::CONFIG_FIELD_ENUM_MASK,
            "registry has more fields than configId's enum field can index");
    }
};
}

namespace
{
// One call site's arguments, by name; a vector beats a map at this size.
using Args = std::vector<std::pair<std::string, std::string>>;

// Holds the emit state -- nodes, blob, registry slots -- threaded through every step.
struct TreeEmitter
{
    std::vector<ModeEntryNode> modes;
    std::vector<CommandNode> nodes;
    std::vector<StrRef> strs;
    std::vector<char> blob;

    /// Hashes a key by its text, so a string_view looks up a string entry.
    struct StrHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    /// Interned text to its id; see internStr.
    std::unordered_map<std::string, uint16_t, StrHash, std::equal_to<>> strIds;

    std::unordered_map<std::string, const JsonNode*> variables;
    std::deque<Args> argStore;

    // Deferral keys in first-seen order; a key's position is the id configExt
    // holds. Discovered from the grammar rather than declared, so the table is
    // exactly the keys that were used, and one name always numbers the same way.
    std::vector<std::string> deferKeys;

    explicit TreeEmitter(const JsonNode* varsRoot)
    {
        config::forEachRegistryId(config::RegistryEntries{}, RegBuilder{});

        if (varsRoot)
            for (const JsonNode& v : varsRoot->children)
                if (isCommandArray(v)) variables.emplace(v.name, &v);
    }

    /**
     * @brief Interns one string and returns its id.
     *
     * The grammar repeats itself heavily -- "<cr>" alone lands here 19.5k times,
     * and descriptions are copied verbatim across every mode that shares a
     * command -- so identical text is stored once and every user gets the same
     * id. An empty string is absent rather than interned, which is what lets a
     * node distinguish "no description" from one that happens to be blank.
     */
    uint16_t internStr(std::string_view s)
    {
        if (s.empty()) return CommandNode::STR_NONE;

        auto it = strIds.find(s);
        if (it != strIds.end()) return it->second;

        if (strs.size() >= TreePatch::ID_BIAS)
            throw std::runtime_error("cli::grammar: interned string count exceeds "
                + std::to_string(TreePatch::ID_BIAS));

        const uint16_t id = static_cast<uint16_t>(strs.size());
        strs.push_back({static_cast<uint32_t>(blob.size()),
                        static_cast<uint32_t>(s.size())});
        blob.insert(blob.end(), s.begin(), s.end());

        strIds.emplace(std::string(s), id);
        return id;
    }

    /// The text behind an id, for diagnostics emitted while flattening.
    std::string_view strAt(uint16_t id) const
    {
        if (id == CommandNode::STR_NONE || id >= strs.size()) return {};
        return std::string_view(blob.data() + strs[id].off, strs[id].len);
    }

    // The variable a "<name>" refers to, or null when the name is not one.
    const JsonNode* variableFor(std::string_view nm) const
    {
        if (nm.size() <= 2 || nm.front() != '<' || nm.back() != '>') return nullptr;
        auto it = variables.find(std::string(nm.substr(1, nm.size() - 2)));
        return it == variables.end() ? nullptr : it->second;
    }

    const Args& internArgs(const JsonNode& obj, const std::string& site,
                           const Args* outer)
    {
        if (obj.type != JsonNode::OBJECT)
            throw std::runtime_error("cli::grammar: '" + site + "' has a non-object "
                + std::string(KEY_ARGS));

        Args a;
        for (const JsonNode& p : obj.children)
        {
            if (p.type != JsonNode::STRING)
                throw std::runtime_error("cli::grammar: '" + site + "' argument '"
                    + p.name + "' is not a string");

            for (const auto& [k, _] : a)
                if (k == p.name)
                    throw std::runtime_error("cli::grammar: '" + site
                        + "' passes argument '" + p.name + "' twice");

            // A "$name" value forwards what this site was itself given.
            if (p.strValue.size() > 1 && p.strValue.front() == ARG_SIGIL)
            {
                std::string_view want(p.strValue);
                want.remove_prefix(1);

                const std::string* found = nullptr;
                if (outer)
                    for (const auto& [an, av] : *outer)
                        if (an == want) { found = &av; break; }

                if (found) a.emplace_back(p.name, *found);
                continue;
            }

            a.emplace_back(p.name, p.strValue);
        }

        argStore.push_back(std::move(a));
        return argStore.back();
    }

    // One child slot after variable expansion, with the args in scope for it.
    struct Slot
    {
        const JsonNode* src;
        const JsonNode* cont;
        const Args* args;
    };

    // One queued node waiting to be emitted, plus the context it inherits.
    struct Frame
    {
        uint32_t idx;
        const JsonNode* src;
        const JsonNode* cont; // Site subcommands to graft at a dead-end.
        const Args* args;     // What the call site passed in, if anything.
        uint16_t expect = REGISTRY_ANY;
    };

    // No ancestor has rescoped, so any registry is in scope.
    static constexpr uint16_t REGISTRY_ANY = UINT16_MAX;

    // Expands variable references into slots; active catches one reaching itself.
    void resolveChildren(const JsonNode& kids, const JsonNode* cont,
                         const Args* args, std::vector<const JsonNode*>& active,
                         std::vector<Slot>& out)
    {
        for (const JsonNode& c : kids.children)
        {
            const JsonNode* name = member(c, KEY_NAME);
            const JsonNode* var = (name && name->type == JsonNode::STRING)
                                ? variableFor(name->strValue) : nullptr;
            if (!var)
            {
                out.push_back({&c, cont, args});
                continue;
            }

            // A variable that reaches itself would expand forever.
            if (std::find(active.begin(), active.end(), var) != active.end())
                throw std::runtime_error("cli::grammar: variable '"
                    + name->strValue + "' references itself");

            const JsonNode* subs = member(c, KEY_SUBCOMMANDS);
            const JsonNode* siteCont = (subs && !subs->children.empty()) ? subs : cont;

            const Args* siteArgs = args;
            if (const JsonNode* a = member(c, KEY_ARGS))
                siteArgs = &internArgs(*a, name->strValue, args);

            active.push_back(var);
            resolveChildren(*var, siteCont, siteArgs, active, out);
            active.pop_back();
        }
    }

    // Reserves a contiguous run for one node's children and queues them.
    void emitChildren(std::deque<Frame>& queue, CommandNode& n,
                      const JsonNode& kids, const JsonNode* cont,
                      const Args* args, uint16_t expect)
    {
        std::vector<const JsonNode*> active;
        std::vector<Slot> slots;
        resolveChildren(kids, cont, args, active, slots);
        if (slots.empty()) return;

        uint32_t count = static_cast<uint32_t>(slots.size());
        if (count > UINT16_MAX)
            throw std::runtime_error("cli::grammar: subcommand count exceeds uint16");

        // Checked on the spliced width; expansion is what pushes a set past the
        // mask. `recursive` is a parent property, and the parent's flags are
        // already resolved by the time its children are emitted.
        if (count > MAX_TRACKED_SIBLINGS && n.has(CommandNode::RECURSIVE))
            throw std::runtime_error("cli::grammar: recursive set of "
                + std::to_string(count) + " exceeds the "
                + std::to_string(MAX_TRACKED_SIBLINGS) + " sibling mask limit");

        uint32_t firstChild = static_cast<uint32_t>(nodes.size());
        nodes.resize(nodes.size() + count);
        n.subcmdOff = firstChild;
        n.subcmdSiz = static_cast<uint16_t>(count);
        for (uint32_t i = 0; i < count; ++i)
            queue.push_back({firstChild + i, slots[i].src,
                             slots[i].cont, slots[i].args, expect});
    }

    /**
     * @brief Binds the two-part "Schema::member" spelling, which names the field only
     * by the tuple schema it stores. Returns false when the first token is not
     * a schema name at all, leaving the key to be read as "Registry::field".
     */
    bool bindTupleSchema(CommandNode& n, const ConfigKey& key,
                         const std::string& cfg, const std::string& emitName)
    {
        config::TupleFieldLookup found = config::findTupleField(
            config::RegistryEntries{}, config::tokenHash(key.registry));

        if (found.count == 0) return false;

        if (!found.oneRegistry)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names tuple schema '" + std::string(key.registry)
                + "', which " + std::to_string(found.count)
                + " fields across separate registries store; name the field"
                " instead, as \"Registry::field::" + std::string(key.field) + "\"");

        uint16_t memberIdx = config::findTupleMemberAt(
            config::RegistryEntries{}, found.registry, found.field,
            config::tokenHash(key.field));

        if (memberIdx == config::TUPLE_NOT_FOUND)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names member '" + std::string(key.field) + "', which '"
                + std::string(key.registry) + "' does not declare");

        if (memberIdx >= CommandNode::CONFIG_EXT_NONE)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' tuple member index " + std::to_string(memberIdx)
                + " does not fit configExt");

        n.configId = CommandNode::packConfig(found.registry, found.field);
        n.configExt = static_cast<uint8_t>(memberIdx);
        n.flags |= CommandNode::TUPLE_CHANGE;

        return true;
    }

    // Binds "Registry::field[::member]" or "Schema::member".
    void bindConfig(CommandNode& n, const std::string& cfg,
                    const std::string& emitName, std::string& cfgKeyText)
    {
        cfgKeyText = cfg;

        ConfigKey key = splitConfigKey(cfg);

        // A two-part key naming a known schema is a tuple member; anything else
        // is a registry and field, including a two-part key that is not one.
        if (key.member.empty() && !key.field.empty())
        {
            if (bindTupleSchema(n, key, cfg, emitName))
                return;
        }

        uint16_t reg = resolveRegistry(key.registry, emitName);

        if (key.field.empty())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names registry '" + std::string(key.registry)
                + "' without a field; use \"Registry::field\"");

        uint16_t fieldIdx = resolveField(reg, key.registry, key.field, emitName);

        if (reg > CommandNode::CONFIG_FIELD_MAX_REGISTRY)
            throw std::runtime_error("cli::grammar: registry id " + std::to_string(reg)
                + " exceeds what configId can encode; raise CONFIG_FIELD_ENUM_BITS");

        n.configId = CommandNode::packConfig(reg, fieldIdx);

        if (!key.member.empty())
        {
            uint16_t memberIdx = config::findTupleMemberAt(
                config::RegistryEntries{}, reg, fieldIdx,
                config::tokenHash(key.member));

            if (memberIdx == config::TUPLE_NOT_FOUND)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' names tuple member '" + std::string(key.member)
                    + "' of '" + std::string(key.registry) + "::"
                    + std::string(key.field) + "', which has no such member"
                    + " (or names no tuple schema)");

            if (memberIdx >= CommandNode::CONFIG_EXT_NONE)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' tuple member index " + std::to_string(memberIdx)
                    + " does not fit configExt");

            n.configExt = static_cast<uint8_t>(memberIdx);
            n.flags |= CommandNode::TUPLE_CHANGE;
        }
    }

    // Sets the bound field to one member of its enum type.
    void bindEnum(CommandNode& n, const std::string& en,
                  const std::string& emitName, const std::string& cfgKeyText)
    {
        if (!n.hasConfig())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names enum '" + en + "' but no "
                + std::string(KEY_CONFIG) + "; an enum needs the field it sets");

        ConfigKey ek = splitConfigKey(en);
        if (ek.field.empty() || !ek.member.empty())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' enum '" + en
                + "' is not of the form \"Type::MEMBER\"");

        // On a tuple member the member's own type is the authority, not the
        // field's: the field stores a whole tuple and is never an enum itself.
        const bool onTupleMember = n.has(CommandNode::TUPLE_CHANGE);

        config::EnumResolution res = config::resolveEnumAt(
            config::RegistryEntries{},
            n.fieldRegistryId(), n.enumIndex(),
            onTupleMember ? n.configExt : config::TUPLE_NOT_FOUND,
            config::tokenHash(ek.registry), config::tokenHash(ek.field));

        if (!res.fieldIsEnum)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names enum '" + en + "' but its "
                + (onTupleMember ? "tuple member" : "field") + " '"
                + cfgKeyText + "' is not a registered enum type");

        if (!res.typeMatched)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names enum type '" + std::string(ek.registry)
                + "' but its field is '" + std::string(res.fieldTypeName) + "'");

        if (res.index == config::ENUM_NOT_FOUND)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names member '" + std::string(ek.field)
                + "', which '" + std::string(res.fieldTypeName) + "' does not declare");

        // A tuple member keeps its own index and gains the enum's, four bits
        // each, so both are checked against the half rather than the byte.
        if (onTupleMember)
        {
            const uint8_t member = n.configExt;

            if (member > CommandNode::TUPLE_ENUM_MAX)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' tuple member index " + std::to_string(member)
                    + " does not fit an enum-valued member, which holds "
                    + std::to_string(CommandNode::TUPLE_ENUM_BITS) + " bits");

            if (res.index > CommandNode::TUPLE_ENUM_MAX)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' enum member index " + std::to_string(res.index)
                    + " does not fit a tuple member, which holds "
                    + std::to_string(CommandNode::TUPLE_ENUM_BITS) + " bits");

            n.configExt = CommandNode::packTupleEnum(member, res.index);
            n.flags |= CommandNode::TUPLE_ENUM;
            return;
        }

        if (res.index >= CommandNode::CONFIG_EXT_NONE)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' enum member index " + std::to_string(res.index)
                + " does not fit configExt");

        n.configExt = static_cast<uint8_t>(res.index);
        n.flags |= CommandNode::ENUM_CHANGE;
        if (res.isBitMap)
            n.flags |= CommandNode::ENUM_BITMAP;
    }

    /**
     * @brief The id a deferral key is stored under, assigning one if the key is new.
     *
     * Numbered rather than hashed. configExt is a byte with 0xFF spoken for, so
     * a hash would have to be truncated into 255 values and two keys colliding
     * would resolve each other's -- silently, and only in a grammar large enough
     * to reach the collision. Positions cannot collide, and running out of them
     * is a build error rather than a wrong answer at runtime.
     */
    uint8_t deferKeyId(const std::string& key, const std::string& emitName,
                       std::string_view which)
    {
        if (key.empty())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' has an empty " + std::string(which));

        for (size_t i = 0; i < deferKeys.size(); ++i)
            if (deferKeys[i] == key) return static_cast<uint8_t>(i);

        if (deferKeys.size() >= CommandNode::CONFIG_EXT_NONE)
            throw std::runtime_error("cli::grammar: '" + emitName + "' names key '"
                + key + "', which is deferral key "
                + std::to_string(deferKeys.size())
                + "; configExt holds "
                + std::to_string(CommandNode::CONFIG_EXT_NONE));

        deferKeys.push_back(key);
        return static_cast<uint8_t>(deferKeys.size() - 1);
    }

    /**
     * @brief Holds this command's value under a key instead of writing it now.
     *
     * The field is still named and still checked -- what the key changes is when
     * the write lands, not where. configExt carries the key's id, which is why
     * this cannot sit alongside an enum, tuple or mode binding: each of those
     * wants the same byte for its own meaning.
     */
    void bindDeferred(CommandNode& n, const std::string& key,
                      const std::string& emitName, const std::string& cfgKeyText)
    {
        if (!n.hasConfig())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' defers under '" + key + "' but names no "
                + std::string(KEY_CONFIG)
                + "; a deferred value needs the field it resolves into");

        if (n.flags & (CommandNode::ENUM_CHANGE | CommandNode::TUPLE_CHANGE))
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' defers under '" + key + "' and also sets an enum or tuple"
                " member of '" + cfgKeyText + "'; configExt holds one");

        n.configExt = deferKeyId(key, emitName, KEY_DEFERRED);
        n.flags |= CommandNode::DEFERRED;
    }

    /**
     * @brief Marks this command as the source a deferred key waits on.
     *
     * Numbered out of the same table bindDeferred uses, so the ids pair up. It
     * binds no field: what it resolves is whatever deferred commands named the
     * key, which may sit anywhere in the grammar and in any registry.
     */
    void bindResolver(CommandNode& n, const std::string& key,
                      const std::string& emitName, const std::string& cfgKeyText)
    {
        if (n.flags & CommandNode::DEFERRED)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' is both deferred and a resolver; configExt holds one key");

        if (n.flags & (CommandNode::ENUM_CHANGE | CommandNode::TUPLE_CHANGE))
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' resolves '" + key + "' and also sets an enum or tuple member"
                " of '" + cfgKeyText + "'; configExt holds one");

        n.configExt = deferKeyId(key, emitName, KEY_RESOLVER);
        n.flags |= CommandNode::RESOLVER;
    }

    /**
     * @brief Populates one CommandNode from its JSON object and returns its children.
     *
     * Validates the required `name`/`description` keys, resolves any `$var`
     * references against @p args, and writes the node's flags, config binding
     * and properties. The returned pointer is pushed back onto the breadth-first
     * queue that drives tree construction.
     *
     * @param n      Node to fill in; must already be allocated at @p idx.
     * @param idx    This node's index in the flat node array.
     * @param src    JSON object describing the command.
     * @param args   Variable bindings from the calling definition, or `nullptr`.
     * @param expect In/out. On entry, the registry this node must bind to, or
     *               `REGISTRY_ANY`. Narrowed to the mode's registry when the
     *               node is a mode boundary, constraining its whole subtree.
     * @return The node's `subcommands` array, or `nullptr` if it is a leaf.
     * @throws std::runtime_error On any malformed or contradictory grammar
     *         object; the message names the offending command.
     */
    const JsonNode* emitCommand(CommandNode& n, uint32_t idx,
                                const JsonNode& src, const Args* args,
                                uint16_t& expect)
    {
        rejectUnknownKeys(src);

        const JsonNode* name = member(src, KEY_NAME);
        if (!name)
            throw std::runtime_error("cli::grammar: command object missing '"
                + std::string(KEY_NAME) + "'");
        if (name->type != JsonNode::STRING)
            throw std::runtime_error("cli::grammar: command object has a non-string '"
                + std::string(KEY_NAME) + "'");
        if (name->strValue.empty())
            throw std::runtime_error("cli::grammar: command object has an empty '"
                + std::string(KEY_NAME) + "'");

        const JsonNode* desc = member(src, KEY_DESCRIPTION);
        if (!desc)
            throw std::runtime_error("cli::grammar: '" + name->strValue
                + "' has no '" + std::string(KEY_DESCRIPTION) + "'");
        if (desc->type != JsonNode::STRING)
            throw std::runtime_error("cli::grammar: '" + name->strValue
                + "' has a non-string '" + std::string(KEY_DESCRIPTION) + "'");

        const std::string& emitName = name->strValue;

        bool argUnbound = false;

        auto keyValue = [&](std::string_view key) -> const std::string*
        {
            const JsonNode* k = member(src, key);
            if (!k) return nullptr;

            if (k->type != JsonNode::STRING)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' has a non-string " + std::string(key));

            if (k->strValue.empty() || k->strValue.front() != ARG_SIGIL)
                return &k->strValue;

            std::string_view want(k->strValue);
            want.remove_prefix(1);

            if (args)
                for (const auto& [an, av] : *args)
                    if (an == want) return &av;

            argUnbound = true;
            return nullptr;
        };

        n.nameId = internStr(emitName);
        n.descId = internStr(desc->strValue);

        if (const JsonNode* props = member(src, KEY_PROPERTIES))
            for (const JsonNode& p : props->children)
                n.flags |= propertyFlag(p.strValue);

        // "exit" and "end" are the grammar's only mode exits.
        if (emitName == "exit" || emitName == "end")
            n.flags |= CommandNode::MODE_EXIT;

        if (const JsonNode* sup = member(src, KEY_SUPPORT))
        {
            if (sup->type != JsonNode::BOOL)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' has a non-boolean " + std::string(KEY_SUPPORT));
        }

        std::string cfgKeyText;

        // "Registry::field" binds this command to a config field
        if (const std::string* cfg = keyValue(KEY_CONFIG))
        {
            bindConfig(n, *cfg, emitName, cfgKeyText);

            if (expect != REGISTRY_ANY && n.fieldRegistryId() != expect)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' binds '" + cfgKeyText + "', but an outer command rescoped"
                    " to registry " + std::to_string(expect)
                    + "; every config below a rescope names that registry");
        }

        // A shared definition names fields its caller fills in, so a caller that
        // leaves them out drops the binding rather than failing: the keys below
        // set a field this node no longer has, and go quiet along with it.
        const bool configDropped = argUnbound && !n.hasConfig();

        // "Type::MEMBER" sets the bound field to one enum member.
        if (const std::string* en = keyValue(KEY_ENUM); en && !configDropped)
            bindEnum(n, *en, emitName, cfgKeyText);

        // Both name a deferral key, and both land in configExt, so they run
        // after the bindings that also claim it and refuse to share.
        if (const std::string* key = keyValue(KEY_DEFERRED); key && !configDropped)
            bindDeferred(n, *key, emitName, cfgKeyText);

        if (const std::string* key = keyValue(KEY_RESOLVER); key && !configDropped)
            bindResolver(n, *key, emitName, cfgKeyText);

        // "prompt/variant" makes this command enter that mode.
        if (const std::string* md = keyValue(KEY_MODE))
        {
            if (n.hasModeExit())
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' enters mode '" + *md + "' and also exits a mode");

            if (!n.hasConfig())
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' enters mode '" + *md + "' but names no "
                    + std::string(KEY_CONFIG) + "; a mode change needs the "
                    "field its registry comes from");

            if (n.flags & (CommandNode::TUPLE_CHANGE | CommandNode::ENUM_CHANGE))
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' enters a mode and also sets an enum or tuple member;"
                    " configExt holds one");

            if (n.flags & (CommandNode::DEFERRED | CommandNode::RESOLVER))
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' enters a mode and also names a deferral key;"
                    " configExt holds one");

            n.configExt = static_cast<uint8_t>(resolveMode(*md, emitName));
            n.flags |= CommandNode::MODE_CHANGE;
        }

        if (n.hasConfig() && !(n.flags & CommandNode::MODE_CHANGE))
        {
            config::FieldScope scope = config::resolveScopeAt(
                config::RegistryEntries{}, n.fieldRegistryId(), n.enumIndex());

            if (scope.isContainer)
            {
                // A member named on a container would never be written, since
                // resolving to a registry is all the node does.
                if (n.flags & (CommandNode::TUPLE_CHANGE | CommandNode::ENUM_CHANGE))
                    throw std::runtime_error("cli::grammar: '" + emitName
                        + "' binds container '" + cfgKeyText + "' and also sets an"
                        " enum or tuple member; a rescope writes no value");

                // A resolver names no field of its own, so a container binding
                // on one has nothing to rescope through.
                if (n.flags & CommandNode::RESOLVER)
                    throw std::runtime_error("cli::grammar: '" + emitName
                        + "' binds container '" + cfgKeyText + "' and also resolves a"
                        " deferral key; a resolver names no field to rescope through");

                if (scope.registry >= config::registryCount)
                    throw std::runtime_error("cli::grammar: '" + emitName
                        + "' binds container '" + cfgKeyText + "', whose entries are"
                        " not a registered registry");

                n.flags |= CommandNode::REGISTRY_CHANGE;

                // Everything below now writes into what was rescoped to,
                // which supersedes whatever an outer rescope established.
                expect = scope.registry;
            }
        }

        return member(src, KEY_SUBCOMMANDS);
    }

    // Walks a command list breadth-first, emitting each node as it is dequeued.
    uint32_t appendCommands(const JsonNode& array)
    {
        uint32_t rootIdx = static_cast<uint32_t>(nodes.size());
        nodes.emplace_back();

        std::deque<Frame> queue;
        queue.push_back({rootIdx, &array, nullptr, nullptr});

        while (!queue.empty())
        {
            auto [idx, src, cont, args, expect] = queue.front();
            queue.pop_front();

            CommandNode n{};

            // Synthetic array roots carry no grammar of their own.
            const JsonNode* subs = src->type == JsonNode::ARRAY
                                 ? src
                                 : emitCommand(n, idx, *src, args, expect);

            if (subs && !subs->children.empty())
                emitChildren(queue, n, *subs, cont, args, expect);
            else if (cont)
                emitChildren(queue, n, *cont, nullptr, nullptr, expect);

            nodes[idx] = n;
        }

        return rootIdx;
    }

    // One entry per command list: the mode name plus the key of the submodes reached through.
    void addEntry(const std::string& modeName, const std::string& subName,
                  const JsonNode& array, uint16_t registry,
                  const std::string& prompt)
    {
        ModeEntryNode e{};
        e.registryId = registry;

        e.nameId = internStr(modeName);
        e.subId = internStr(subName);
        e.promptId = internStr(prompt);

        e.cmdOff = appendCommands(array);
        e.cmdSiz = nodes[e.cmdOff].subcmdSiz;
        modes.push_back(e);
    }

    /**
     * @brief Rejects a deferral key that only one side of the grammar names.
     *
     * Keys are discovered rather than declared, so nothing has yet checked that
     * a name was spelled the same in both places -- a typo reads as a new key,
     * and both halves flatten cleanly into a pairing that can never resolve.
     * Run once the whole grammar is walked: the two sides may sit in different
     * files, so neither is complete until every mode has been emitted.
     */
    void checkDeferKeys() const
    {
        std::vector<bool> deferred(deferKeys.size(), false);
        std::vector<bool> resolved(deferKeys.size(), false);

        for (const CommandNode& n : nodes)
        {
            if (n.hasDeferred()) deferred[n.deferKey()] = true;
            if (n.hasResolver()) resolved[n.deferKey()] = true;
        }

        for (size_t i = 0; i < deferKeys.size(); ++i)
        {
            if (deferred[i] && !resolved[i])
                throw std::runtime_error("cli::grammar: key '" + deferKeys[i]
                    + "' is deferred under but nothing resolves it; a "
                    + std::string(KEY_DEFERRED) + " needs a matching "
                    + std::string(KEY_RESOLVER));

            if (resolved[i] && !deferred[i])
                throw std::runtime_error("cli::grammar: key '" + deferKeys[i]
                    + "' is resolved but nothing defers under it; a "
                    + std::string(KEY_RESOLVER) + " needs a matching "
                    + std::string(KEY_DEFERRED));
        }
    }

    /**
     * @brief Reports a field that one line could write twice.
     *
     * Many nodes may write one field without any of them conflicting. The
     * branches of an interface name each bind the same field and only one can
     * match; a shared definition binds one field from every call site that
     * expands it; whole modes repeat a binding that is reached by different
     * paths. None of those is ambiguous, because a line walks a single
     * root-to-leaf path and meets exactly one of them.
     *
     * The test is therefore reachability rather than count: two writers matter
     * only when one is an ancestor of the other, which is the one arrangement a
     * single line can traverse both of. Then whichever runs second silently
     * wins and the first appears to do nothing. Enum members, tuple members and
     * deferred values are excluded outright, being separated by something other
     * than the field they share, as is a parent and its immediate child, which
     * is how a two-token key such as `Vlan 10` is written.
     *
     * A warning rather than an error because the grammar is edited in bulk and
     * a half-finished mode is a normal intermediate state. Runs after the whole
     * walk, since the two commands may sit in different files.
     */
    void reportSharedFields() const
    {
        constexpr uint16_t shared = CommandNode::ENUM_CHANGE
                                  | CommandNode::TUPLE_CHANGE
                                  | CommandNode::DEFERRED
                                  | CommandNode::RESOLVER;

        constexpr uint32_t NO_PARENT = ~uint32_t{0};
        std::vector<uint32_t> parent(nodes.size(), NO_PARENT);

        for (size_t i = 0; i < nodes.size(); ++i)
            for (uint32_t c = 0; c < nodes[i].subcmdSiz; ++c)
            {
                const uint32_t child = nodes[i].subcmdOff + c;
                if (child < parent.size()) parent[child] = static_cast<uint32_t>(i);
            }

        auto collides = [&](uint32_t a, uint32_t b)
        {
            for (uint32_t up = a; up != NO_PARENT; up = parent[up])
                if (up == b) return parent[a] != b;
            for (uint32_t up = b; up != NO_PARENT; up = parent[up])
                if (up == a) return parent[b] != a;
            return false;
        };

        std::unordered_map<uint16_t, std::vector<uint32_t>> writers;

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const CommandNode& n = nodes[i];
            if (!n.hasConfig() || (n.flags & shared)) continue;
            writers[n.configId].push_back(static_cast<uint32_t>(i));
        }

        for (const auto& [id, found] : writers)
        {
            std::vector<uint32_t> clash;
            for (size_t a = 0; a < found.size() && clash.empty(); ++a)
                for (size_t b = a + 1; b < found.size(); ++b)
                    if (collides(found[a], found[b]))
                    {
                        clash = { found[a], found[b] };
                        break;
                    }

            if (clash.empty()) continue;

            const CommandNode probe{ .configId = id };

            std::string msg = "cli::grammar: warning: registry "
                + std::to_string(probe.fieldRegistryId()) + " field "
                + std::to_string(probe.enumIndex())
                + " is written twice on one line, and the last one run wins:";

            for (const uint32_t i : clash)
                msg += " '" + std::string(strAt(nodes[i].nameId)) + "'";

            std::fprintf(stderr, "%s\n", msg.c_str());
        }
    }
};
}

namespace
{
// Flattens modes into the serialized tree; shared by both entry points.
std::vector<std::byte> emitTree(const std::vector<ModeFile>& modeFiles,
                                const JsonNode* varsRoot,
                                uint32_t grammarHash = 0)
{
    TreeEmitter em(varsRoot);

    for (const ModeFile& m : modeFiles)
        em.addEntry(m.name, m.variant, *m.commands, m.registry, m.prompt);

    em.checkDeferKeys();
    em.reportSharedFields();

    auto& modes = em.modes;
    auto& nodes = em.nodes;
    auto& strs  = em.strs;
    auto& blob  = em.blob;

    if (modes.size() > UINT32_MAX)
        throw std::runtime_error("cli::grammar: mode count exceeds uint32");

    FileHeader header{};
    header.modeCount = static_cast<uint32_t>(modes.size());
    header.nodeCount = static_cast<uint32_t>(nodes.size());
    header.strCount = static_cast<uint32_t>(strs.size());
    header.blobSize = static_cast<uint32_t>(blob.size());
    header.registryHash = config::REGISTRY_FIELD_HASH;
    header.grammarHash = grammarHash;

    std::vector<std::byte> out(sizeof(FileHeader)
                             + modes.size() * sizeof(ModeEntryNode)
                             + nodes.size() * sizeof(CommandNode)
                             + strs.size() * sizeof(StrRef)
                             + blob.size());
    std::byte* p = out.data();
    auto write = [&](const void* src, size_t bytes)
    {
        if (bytes) std::memcpy(p, src, bytes);
        p += bytes;
    };
    write(&header, sizeof(FileHeader));
    write(modes.data(), modes.size() * sizeof(ModeEntryNode));
    write(nodes.data(), nodes.size() * sizeof(CommandNode));
    write(strs.data(), strs.size() * sizeof(StrRef));
    write(blob.data(), blob.size());
    return out;
}
}

namespace
{
std::vector<std::filesystem::path> grammarFiles(const std::string& dir)
{
    namespace fs = std::filesystem;

    std::vector<fs::path> paths;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".json")
            paths.push_back(e.path());

    std::sort(paths.begin(), paths.end());
    return paths;
}
}

uint32_t hashDir(const std::string& dir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    uint32_t h = 0x811C9DC5u;
    auto mix = [&h](std::string_view bytes)
    {
        for (const char c : bytes)
        {
            h ^= static_cast<uint8_t>(c);
            h *= 0x01000193u;
        }
    };

    for (const fs::path& p : grammarFiles(dir))
    {
        std::ifstream in(p, std::ios::binary);
        if (!in) return 0;

        std::ostringstream body;
        body << in.rdbuf();
        if (!in && !in.eof()) return 0;

        mix(fs::relative(p, dir, ec).generic_string());
        mix(body.str());
    }

    // Reserved for "could not read", so a real hash never collides with it.
    return h ? h : 1u;
}

std::vector<std::byte> flattenDir(const std::string& dir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        throw std::runtime_error("cli::grammar::flattenDir: not a directory: " + dir);

    const std::vector<fs::path> paths = grammarFiles(dir);

    // Deque so growing it never moves an element a command pointer refers to.
    std::deque<JsonNode> docs;
    std::vector<ModeFile> files;
    files.reserve(paths.size());

    const JsonNode* varsRoot = nullptr;

    for (const fs::path& p : paths)
    {
        const std::string stem = p.stem().string();
        const std::string who = p.filename().string();

        JsonNode& dom = docs.emplace_back(utils::json::load(p.string()));

        // The variable file is a shared substitution table, not a mode.
        if (stem == VARIABLES_STEM)
        {
            varsRoot = &dom;
            continue;
        }

        if (dom.type != JsonNode::OBJECT)
            throw std::runtime_error("cli::grammar::flattenDir: '" + who
                + "' must hold an object");

        // Neither key means it is not a mode file; one without the other is half-written.
        const JsonNode* pr = member(dom, KEY_PROMPT);
        const JsonNode* rg = memberLoose(dom, KEY_REGISTRY);
        const JsonNode* cmds = member(dom, KEY_COMMANDS);
        if (!pr && !cmds) continue;
        if (!pr || !cmds)
            throw std::runtime_error("cli::grammar::flattenDir: '" + who
                + "' has " + std::string(pr ? KEY_PROMPT : KEY_COMMANDS)
                + " but no " + std::string(pr ? KEY_COMMANDS : KEY_PROMPT));

        if (cmds->type != JsonNode::ARRAY)
            throw std::runtime_error("cli::grammar::flattenDir: '" + who
                + "' has a non-array " + std::string(KEY_COMMANDS));
        if (pr->type != JsonNode::STRING)
            throw std::runtime_error("cli::grammar::flattenDir: '" + who
                + "' has a non-string " + std::string(KEY_PROMPT));

        // Keyed by prompt, not filename: that is what getMode looks up.
        ModeFile m;
        m.name = pr->strValue;
        m.commands = cmds;
        m.prompt = pr->strValue;
        m.origin = who;

        // Optional: a mode binding no config fields names no registry.
        if (rg) m.registry = modeRegistryName(*rg, who);

        // Separates modes sharing a prompt; absent when a prompt has only one.
        if (const JsonNode* v = member(dom, KEY_VARIANT))
        {
            if (v->type != JsonNode::STRING)
                throw std::runtime_error("cli::grammar::flattenDir: '" + who
                    + "' has a non-string " + std::string(KEY_VARIANT));
            m.variant = v->strValue;
        }

        // A variant dict lets one file contribute several rows sharing its prompt and registry.
        if (isCommandArray(*cmds))
        {
            files.push_back(std::move(m));
            continue;
        }

        if (!m.variant.empty())
            throw std::runtime_error("cli::grammar::flattenDir: '" + who
                + "' names a " + std::string(KEY_VARIANT)
                + " and also wraps its commands in a variant list; use one or the other");

        const JsonNode& dict = variantModeDict(*cmds, who);
        for (const JsonNode& branch : dict.children)
        {
            if (!isCommandArray(branch))
                throw std::runtime_error("cli::grammar::flattenDir: variant '"
                    + branch.name + "' of '" + who
                    + "' must hold commands; variants cannot nest");

            ModeFile mv = m;
            mv.variant = branch.name;
            mv.commands = &branch;
            files.push_back(std::move(mv));
        }
    }

    if (files.empty())
        throw std::runtime_error("cli::grammar::flattenDir: no mode files under " + dir);

    // A duplicate prompt+variant makes the later row unreachable, since findMode returns the first.
    std::unordered_map<std::string, std::string> seen;
    for (const ModeFile& m : files)
    {
        std::string key = m.name + '\0' + m.variant;
        auto [it, fresh] = seen.emplace(std::move(key), m.origin);
        if (!fresh)
            throw std::runtime_error("cli::grammar::flattenDir: '" + m.origin
                + "' and '" + it->second + "' share prompt '" + m.name + "'"
                + (m.variant.empty()
                    ? " and neither names a " + std::string(KEY_VARIANT)
                    : " and variant '" + m.variant + "'"));
    }

    return emitTree(files, varsRoot, hashDir(dir));
}
}
