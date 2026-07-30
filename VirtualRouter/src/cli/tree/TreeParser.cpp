// TreeParser.cpp

#include <utils/Json.hpp>
#include "CommandTree.h"
#include "Command.h"
#include "FileHeader.hpp"
#include <deque>
#include <cstring>
#include <algorithm>
#include <unordered_map>

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

bool isCommandArray(const JsonNode& n)
{
    return n.type == JsonNode::ARRAY
        && !n.children.empty()
        && n.children.front().type == JsonNode::OBJECT
        && member(n.children.front(), KEY_NAME) != nullptr;
}

const JsonNode& subModeDict(const JsonNode& n, const std::string& who)
{
    if (n.type != JsonNode::ARRAY || n.children.size() != 1
        || n.children.front().type != JsonNode::OBJECT)
        throw std::runtime_error("cli::grammar::flattenCmds: '" + who
            + "' is neither a command array nor a single-object submode list");
    return n.children.front();
}

uint16_t propertyFlag(std::string_view p)
{
    if (p == "negate")               return CommandNode::NEGATE;
    if (p == "negate_all")           return CommandNode::NEGATE_ALL;
    if (p == "nagate_all")           return CommandNode::NEGATE_ALL;
    if (p == "negate_hide")          return CommandNode::NEGATE_HIDE;
    if (p == "negate_show")          return CommandNode::NEGATE_SHOW;
    if (p == "recursive")            return CommandNode::RECURSIVE;
    if (p == "subcmd_single_use")    return CommandNode::SUBCMD_SINGLE_USE;
    if (p == "subcmd_sequence")      return CommandNode::SUBCMD_SEQUENCE;
    throw std::runtime_error("cli::grammar::flattenCmds: unknown property '" + std::string(p) + "'");
}
}

std::vector<std::byte> flattenCmds(const utils::json::JsonNode& root)
{
    if (root.type != JsonNode::OBJECT)
        throw std::runtime_error("cli::grammar::flattenCmds: document root must be an object of modes");

    std::vector<ModeEntryNode> modes;
    std::vector<CommandNode> nodes;
    std::vector<char> blob;

    auto internStr = [&](std::string_view s) -> uint32_t
    {
        uint32_t off = static_cast<uint32_t>(blob.size());
        blob.insert(blob.end(), s.begin(), s.end());
        return off;
    };

    std::unordered_map<std::string, const JsonNode*> variables;
    if (const JsonNode* vars = member(root, KEY_VARIABLES))
        for (const JsonNode& v : vars->children)
            if (isCommandArray(v)) variables.emplace(v.name, &v);

    auto variableFor = [&](std::string_view nm) -> const JsonNode*
    {
        if (nm.size() <= 2 || nm.front() != '<' || nm.back() != '>') return nullptr;
        auto it = variables.find(std::string(nm.substr(1, nm.size() - 2)));
        return it == variables.end() ? nullptr : it->second;
    };

    auto appendCommands = [&](const JsonNode& array) -> uint32_t
    {
        struct Frame
        {
            uint32_t idx;
            const JsonNode* src;
            const JsonNode* cont; // Site subcommands to graft at a dead-end.
        };

        struct Slot
        {
            const JsonNode* src;
            const JsonNode* cont;
        };

        auto resolveChildren = [&](auto&& self, const JsonNode& kids,
                                   const JsonNode* cont,
                                   std::vector<const JsonNode*>& active,
                                   std::vector<Slot>& out) -> void
        {
            for (const JsonNode& c : kids.children)
            {
                const JsonNode* name = member(c, KEY_NAME);
                const JsonNode* var = (name && name->type == JsonNode::STRING)
                                    ? variableFor(name->strValue) : nullptr;
                if (!var)
                {
                    out.push_back({&c, cont});
                    continue;
                }

                // A variable that reaches itself would expand forever.
                if (std::find(active.begin(), active.end(), var) != active.end())
                    throw std::runtime_error("cli::grammar::flattenCmds: variable '"
                        + name->strValue + "' references itself");

                const JsonNode* subs = member(c, KEY_SUBCOMMANDS);
                const JsonNode* siteCont = (subs && !subs->children.empty()) ? subs : cont;

                active.push_back(var);
                self(self, *var, siteCont, active, out);
                active.pop_back();
            }
        };

        uint32_t rootIdx = static_cast<uint32_t>(nodes.size());
        nodes.emplace_back();
        std::deque<Frame> queue;
        queue.push_back({rootIdx, &array, nullptr});

        // Reserves a contiguous run for one node's children and queues them.
        auto emitChildren = [&](CommandNode& n, const JsonNode& kids, const JsonNode* cont)
        {
            std::vector<const JsonNode*> active;
            std::vector<Slot> slots;
            resolveChildren(resolveChildren, kids, cont, active, slots);
            if (slots.empty()) return;

            uint32_t count = static_cast<uint32_t>(slots.size());
            if (count > UINT16_MAX)
                throw std::runtime_error("cli::grammar::flattenCmds: subcommand count exceeds uint16");

            // Checked on the spliced width, since expansion is what can push a
            // tracked set past the mask.
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
                    throw std::runtime_error("cli::grammar::flattenCmds: recursive/sequence set of "
                        + std::to_string(count) + " exceeds the "
                        + std::to_string(MAX_TRACKED_SIBLINGS) + " sibling mask limit");
            }

            uint32_t firstChild = static_cast<uint32_t>(nodes.size());
            nodes.resize(nodes.size() + count);
            n.subcmdOff = firstChild;
            n.subcmdSiz = static_cast<uint16_t>(count);
            for (uint32_t i = 0; i < count; ++i)
                queue.push_back({firstChild + i, slots[i].src, slots[i].cont});
        };

        while (!queue.empty())
        {
            auto [idx, src, cont] = queue.front();
            queue.pop_front();

            CommandNode n{};
            const JsonNode* subs = nullptr;
            if (src->type == JsonNode::ARRAY)
            {
                // Synthetic array roots carry no grammar of their own.
                subs = src;
                n.flags |= CommandNode::SUPPORT;
            }
            else
            {
                const JsonNode* name = member(*src, KEY_NAME);
                if (!name || name->type != JsonNode::STRING)
                    throw std::runtime_error("cli::grammar::flattenCmds: command object missing a string 'name'");

                const JsonNode* desc = member(*src, KEY_DESCRIPTION);
                if (desc && desc->type != JsonNode::STRING)
                    throw std::runtime_error("cli::grammar::flattenCmds: '" + name->strValue + "' has a non-string description");

                const std::string& emitName = name->strValue;

                if (emitName.size() > UINT8_MAX)
                    throw std::runtime_error("cli::grammar::flattenCmds: '" + emitName
                        + "' name exceeds uint8");

                // Interned back to back so desc starts at infoOff + nameSiz.
                n.infoOff = internStr(emitName);
                n.nameSiz = static_cast<uint8_t>(emitName.size());
                if (desc)
                {
                    if (desc->strValue.size() > UINT16_MAX)
                        throw std::runtime_error("cli::grammar::flattenCmds: '" + name->strValue
                            + "' description exceeds uint16");
                    internStr(desc->strValue);
                    n.descSiz = static_cast<uint16_t>(desc->strValue.size());
                }

                if (const JsonNode* props = member(*src, KEY_PROPERTIES))
                    for (const JsonNode& p : props->children)
                        n.flags |= propertyFlag(p.strValue);

                if (const JsonNode* sup = member(*src, KEY_SUPPORT))
                {
                    if (sup->type != JsonNode::BOOL)
                        throw std::runtime_error("cli::grammar::flattenCmds: '" + name->strValue
                            + "' has a non-boolean support");
                    n.flags |= CommandNode::SUPPORT_SET;
                    if (sup->boolValue)
                        n.flags |= CommandNode::SUPPORT;
                }
                else
                {
                    // Absent means supported; the key marks exceptions, not the norm.
                    n.flags |= CommandNode::SUPPORT;
                }

                subs = member(*src, KEY_SUBCOMMANDS);
            }

            if (subs && !subs->children.empty())
            {
                emitChildren(n, *subs, cont);
            }
            else if (cont)
            {
                emitChildren(n, *cont, nullptr);
            }

            nodes[idx] = n;
        }

        return rootIdx;
    };

    // Emits one entry per command list: the mode name plus the delimited key
    // of the submodes walked through to reach it.
    auto addEntry = [&](const std::string& modeName, const std::string& subName,
                        const JsonNode& array)
    {
        ModeEntryNode e{};
        if (modeName.size() > UINT16_MAX || subName.size() > UINT16_MAX)
            throw std::runtime_error("cli::grammar::flattenCmds: mode name '" + modeName
                + "' or submode '" + subName + "' exceeds uint16");

        // Interned back to back so the submode starts at infoOff + modeSiz.
        e.infoOff = internStr(modeName);
        e.modeSiz = static_cast<uint16_t>(modeName.size());
        if (!subName.empty())
        {
            internStr(subName);
            e.subSiz = static_cast<uint16_t>(subName.size());
        }
        e.cmdOff = appendCommands(array);
        e.cmdSiz = nodes[e.cmdOff].subcmdSiz;
        modes.push_back(e);
    };

    for (const JsonNode& mode : root.children)
    {
        if (mode.name == KEY_VARIABLES) continue;

        if (isCommandArray(mode))
        {
            addEntry(mode.name, std::string(), mode);
            continue;
        }

        const JsonNode& dict = subModeDict(mode, mode.name);
        for (const JsonNode& branch : dict.children)
        {
            if (!isCommandArray(branch))
                throw std::runtime_error("cli::grammar::flattenCmds: submode '" + branch.name
                    + "' of '" + mode.name + "' must hold commands; submodes cannot nest");
            addEntry(mode.name, branch.name, branch);
        }
    }

    if (modes.size() > UINT32_MAX)
        throw std::runtime_error("cli::grammar::flattenCmds: mode count exceeds uint32");

    FileHeader header{};
    header.modeCount = static_cast<uint32_t>(modes.size());
    header.nodeCount = static_cast<uint32_t>(nodes.size());
    header.blobSize = static_cast<uint32_t>(blob.size());

    std::vector<std::byte> out(sizeof(FileHeader)
                             + modes.size() * sizeof(ModeEntryNode)
                             + nodes.size() * sizeof(CommandNode)
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
    write(blob.data(), blob.size());
    return out;
}
}
