// TreeParser.cpp

#include <utils/Json.hpp>
#include "CommandTree.h"
#include "nodes/Command.h"
#include "nodes/FileHeader.hpp"
#include "nodes/RegistryEntry.h"
#include "configs/RegistryTable.hpp"
#include "cli/modes/Mode.hpp"
#include <deque>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
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

uint16_t propertyFlag(std::string_view p)
{
    if (p == "negate")               return CommandNode::NEGATE;
    if (p == "negate_all")           return CommandNode::NEGATE_ALL;
    if (p == "negate_hide")          return CommandNode::NEGATE_HIDE;
    if (p == "negate_show")          return CommandNode::NEGATE_SHOW;
    if (p == "recursive")            return CommandNode::RECURSIVE;
    if (p == "subcmd_single_use")    return CommandNode::SUBCMD_SINGLE_USE;
    if (p == "subcmd_sequence")      return CommandNode::SUBCMD_SEQUENCE;
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

// Lays out rows in list order, so a registry's row index is its id; slots start unbound.
struct RegBuilder
{
    std::vector<RegistryEntry>& rows;
    std::vector<uint32_t>& slots;

    template <typename Entry>
    void operator()()
    {
        using Enum = typename Entry::type;
        constexpr std::size_t count = config::registrySlotsV<Enum>;

        static_assert(count <= CommandNode::CONFIG_FIELD_ENUM_MASK,
            "registry has more fields than configId's enum field can index");

        if (slots.size() + count > MAX_SLOTS)
            throw std::runtime_error("cli::grammar: slot table exceeds uint16 offsets");

        RegistryEntry e{};
        e.slotOff = static_cast<uint16_t>(slots.size());
        e.slotCount = static_cast<uint16_t>(count);
        rows.push_back(e);

        slots.resize(slots.size() + count, NO_COMMAND);
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
    std::vector<RegistryEntry> regModes;
    std::vector<uint32_t> regIndexes;
    std::vector<ModeEntryNode> modes;
    std::vector<CommandNode> nodes;
    std::vector<char> blob;

    std::unordered_map<std::string, const JsonNode*> variables;
    std::deque<Args> argStore;

    explicit TreeEmitter(const JsonNode* varsRoot)
    {
        // Rows land in list order, so a row's index is the registry's id.
        regModes.reserve(config::registryCount);
        regIndexes.reserve(config::registrySlotTotal);
        config::forEachRegistryId(config::RegistryEntries{},
                                  RegBuilder{regModes, regIndexes});

        if (varsRoot)
            for (const JsonNode& v : varsRoot->children)
                if (isCommandArray(v)) variables.emplace(v.name, &v);
    }

    uint32_t internStr(std::string_view s)
    {
        uint32_t off = static_cast<uint32_t>(blob.size());
        blob.insert(blob.end(), s.begin(), s.end());
        return off;
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
    };

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
                      const Args* args)
    {
        std::vector<const JsonNode*> active;
        std::vector<Slot> slots;
        resolveChildren(kids, cont, args, active, slots);
        if (slots.empty()) return;

        uint32_t count = static_cast<uint32_t>(slots.size());
        if (count > UINT16_MAX)
            throw std::runtime_error("cli::grammar: subcommand count exceeds uint16");

        // Checked on the spliced width; expansion is what pushes a set past the mask.
        if (count > MAX_TRACKED_SIBLINGS)
        {
            bool tracked = false;
            for (const Slot& s : slots)
                if (const JsonNode* cp = member(*s.src, KEY_PROPERTIES))
                    for (const JsonNode& p : cp->children)
                        if (p.strValue == "recursive"
                         || p.strValue == "subcmd_single_use")
                            tracked = true;
            if (tracked)
                throw std::runtime_error("cli::grammar: recursive/sequence set of "
                    + std::to_string(count) + " exceeds the "
                    + std::to_string(MAX_TRACKED_SIBLINGS) + " sibling mask limit");
        }

        uint32_t firstChild = static_cast<uint32_t>(nodes.size());
        nodes.resize(nodes.size() + count);
        n.subcmdOff = firstChild;
        n.subcmdSiz = static_cast<uint16_t>(count);
        for (uint32_t i = 0; i < count; ++i)
            queue.push_back({firstChild + i, slots[i].src,
                             slots[i].cont, slots[i].args});
    }

    // Binds "Registry::field[::member]" and returns the slot the field owns.
    uint32_t* bindConfig(CommandNode& n, const std::string& cfg,
                         const std::string& emitName, std::string& cfgKeyText)
    {
        cfgKeyText = cfg;

        ConfigKey key = splitConfigKey(cfg);
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

        // Claimed after the enum key is read; sharing the field depends on it.
        return &regIndexes[regModes[reg].slotOff + fieldIdx];
    }

    // Sets the bound field to one member of its enum type.
    void bindEnum(CommandNode& n, const std::string& en,
                  const std::string& emitName, const std::string& cfgKeyText)
    {
        if (!n.hasConfig())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names enum '" + en + "' but no "
                + std::string(KEY_CONFIG) + "; an enum needs the field it sets");

        if (n.has(CommandNode::TUPLE_CHANGE))
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' is both an enum and a tuple member; configExt holds one");

        ConfigKey ek = splitConfigKey(en);
        if (ek.field.empty() || !ek.member.empty())
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' enum '" + en
                + "' is not of the form \"Type::MEMBER\"");

        config::EnumResolution res = config::resolveEnumAt(
            config::RegistryEntries{},
            n.fieldRegistryId(), n.enumIndex(),
            config::tokenHash(ek.registry), config::tokenHash(ek.field));

        if (!res.fieldIsEnum)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names enum '" + en + "' but its field '"
                + cfgKeyText + "' is not a registered enum type");

        if (!res.typeMatched)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names enum type '" + std::string(ek.registry)
                + "' but its field is '" + std::string(res.fieldTypeName) + "'");

        if (res.index == config::ENUM_NOT_FOUND)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' names member '" + std::string(ek.field)
                + "', which '" + std::string(res.fieldTypeName) + "' does not declare");

        if (res.index >= CommandNode::CONFIG_EXT_NONE)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' enum member index " + std::to_string(res.index)
                + " does not fit configExt");

        n.configExt = static_cast<uint8_t>(res.index);
        n.flags |= CommandNode::ENUM_CHANGE;
    }

    // Emits one command object; returns its subcommand array, or null if a leaf.
    const JsonNode* emitCommand(CommandNode& n, uint32_t idx,
                                const JsonNode& src, const Args* args)
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

        if (emitName.size() > UINT8_MAX)
            throw std::runtime_error("cli::grammar: '" + emitName
                + "' name exceeds uint8");

        bool fromArgs = false;

        // An unbound argument reads as absent, which is what makes args optional.
        auto keyValue = [&](std::string_view key) -> const std::string*
        {
            const JsonNode* k = member(src, key);
            if (!k) return nullptr;

            if (k->type != JsonNode::STRING)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' has a non-string " + std::string(key));

            if (k->strValue.empty() || k->strValue.front() != ARG_SIGIL)
                return &k->strValue;

            fromArgs = true;

            std::string_view want(k->strValue);
            want.remove_prefix(1);

            if (args)
                for (const auto& [an, av] : *args)
                    if (an == want) return &av;

            return nullptr;
        };

        // Interned back to back so desc starts at infoOff + nameSiz.
        n.infoOff = internStr(emitName);
        n.nameSiz = static_cast<uint8_t>(emitName.size());
        if (desc->strValue.size() > UINT8_MAX)
            throw std::runtime_error("cli::grammar: '" + name->strValue
                + "' description exceeds uint8");
        internStr(desc->strValue);
        n.descSiz = static_cast<uint8_t>(desc->strValue.size());

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
        uint32_t* cfgSlot = nullptr;

        // "Registry::field" binds this command to a config field
        if (const std::string* cfg = keyValue(KEY_CONFIG))
            cfgSlot = bindConfig(n, *cfg, emitName, cfgKeyText);

        // "Type::MEMBER" sets the bound field to one enum member.
        if (const std::string* en = keyValue(KEY_ENUM))
            bindEnum(n, *en, emitName, cfgKeyText);

        if (cfgSlot && !fromArgs
            && !(n.flags & (CommandNode::ENUM_CHANGE | CommandNode::TUPLE_CHANGE)))
        {
            if (*cfgSlot != NO_COMMAND)
                throw std::runtime_error("cli::grammar: '" + emitName
                    + "' binds '" + cfgKeyText + "', already bound by another command");
            *cfgSlot = idx;
        }

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

            n.configExt = static_cast<uint8_t>(resolveMode(*md, emitName));
            n.flags |= CommandNode::MODE_CHANGE;
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
            auto [idx, src, cont, args] = queue.front();
            queue.pop_front();

            CommandNode n{};

            // Synthetic array roots carry no grammar of their own.
            const JsonNode* subs = src->type == JsonNode::ARRAY
                                 ? src
                                 : emitCommand(n, idx, *src, args);

            if (subs && !subs->children.empty())
                emitChildren(queue, n, *subs, cont, args);
            else if (cont)
                emitChildren(queue, n, *cont, nullptr, nullptr);

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
        if (modeName.size() > UINT16_MAX || subName.size() > UINT16_MAX
            || prompt.size() > UINT16_MAX)
            throw std::runtime_error("cli::grammar: mode name '" + modeName
                + "', submode '" + subName + "' or prompt exceeds uint16");

        e.registryId = registry;

        // Interned back to back so each part starts where the previous ended.
        e.infoOff = internStr(modeName);
        e.modeSiz = static_cast<uint16_t>(modeName.size());
        if (!subName.empty())
        {
            internStr(subName);
            e.subSiz = static_cast<uint16_t>(subName.size());
        }
        if (!prompt.empty())
        {
            internStr(prompt);
            e.promptSiz = static_cast<uint16_t>(prompt.size());
        }
        e.cmdOff = appendCommands(array);
        e.cmdSiz = nodes[e.cmdOff].subcmdSiz;
        modes.push_back(e);
    }
};
}

namespace
{
// Flattens modes into the serialized tree; shared by both entry points.
std::vector<std::byte> emitTree(const std::vector<ModeFile>& modeFiles,
                                const JsonNode* varsRoot)
{
    TreeEmitter em(varsRoot);

    for (const ModeFile& m : modeFiles)
        em.addEntry(m.name, m.variant, *m.commands, m.registry, m.prompt);

    auto& regModes   = em.regModes;
    auto& regIndexes = em.regIndexes;
    auto& modes      = em.modes;
    auto& nodes      = em.nodes;
    auto& blob       = em.blob;

    if (modes.size() > UINT32_MAX)
        throw std::runtime_error("cli::grammar: mode count exceeds uint32");

    FileHeader header{};
    header.modeCount = static_cast<uint32_t>(modes.size());
    header.nodeCount = static_cast<uint32_t>(nodes.size());
    header.blobSize = static_cast<uint32_t>(blob.size());
    header.registryCount = static_cast<uint32_t>(regModes.size());
    header.slotCount = static_cast<uint32_t>(regIndexes.size());
    header.registryHash = config::REGISTRY_FIELD_HASH;

    std::vector<std::byte> out(sizeof(FileHeader)
                             + modes.size() * sizeof(ModeEntryNode)
                             + nodes.size() * sizeof(CommandNode)
                             + regModes.size() * sizeof(RegistryEntry)
                             + regIndexes.size() * sizeof(uint32_t)
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
    write(regModes.data(), regModes.size() * sizeof(RegistryEntry));
    write(regIndexes.data(), regIndexes.size() * sizeof(uint32_t));
    write(blob.data(), blob.size());
    return out;
}
}

std::vector<std::byte> flattenDir(const std::string& dir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        throw std::runtime_error("cli::grammar::flattenDir: not a directory: " + dir);

    // Sorted for a stable table; the directory iterator promises no ordering.
    std::vector<fs::path> paths;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".json")
            paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());

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

    return emitTree(files, varsRoot);
}
}
