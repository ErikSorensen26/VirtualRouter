// CommandTree.cpp

#include "CommandTree.h"
#include "nodes/FileHeader.hpp"
#include "configs/RegistryTable.hpp"
#include <Json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace cli::tree
{
CommandTree::CommandTree(std::vector<std::byte> builtBuffer)
    : storage(std::move(builtBuffer))
{
    bindBase();
}

CommandTree::CommandTree(Storage storage)
    : storage(std::move(storage))
{
    bindBase();
}

CommandTree::CommandTree(const std::string& sourcePath, const std::string& binaryPath)
{
    const uint32_t sources = parser::hashDir(sourcePath);

    auto discard = [this]
    {
        storage = Storage();
        header = nullptr;
        baseModes = {};
        baseNodes = {};
        baseStrs = {};
        baseBlob = {};
    };

    try
    {
        storage = Storage::mapFile(binaryPath);
        bindBase();

        if (sources == 0 || grammarHash() == sources) return;

        discard();
    }
    catch (const std::runtime_error&)
    {
        discard();
    }

    build(sourcePath, binaryPath);
    storage = Storage::mapFile(binaryPath);
    bindBase();
}

uint32_t CommandTree::grammarHash() const
{
    return header ? header->grammarHash : 0;
}

PortCounts CommandTree::readPortCounts(const std::string& hwConfigPath)
{
    PortCounts counts;
    try
    {
        utils::json::JsonNode dom = utils::json::load(hwConfigPath);
        if (!dom.isObject() || !dom.contains("Interface")) return counts;

        utils::json::JsonNode interfaces = dom["Interface"];
        if (!interfaces.isObject()) return counts;

        for (const auto& entry : interfaces)
            if (entry.isArray())
                counts.emplace(entry.name, entry.children.size());
    }
    catch (const std::exception&)
    {
        counts.clear();
    }
    return counts;
}

void CommandTree::applyPortCounts(const PortCounts& counts)
{
    if (counts.empty()) return;

    for (uint32_t parentIdx = 0; parentIdx < baseNodes.size(); ++parentIdx)
    {
        const CommandNode& parent = baseNodes[parentIdx];
        if (parent.subcmdSiz == 0 || parent.nameId == CommandNode::STR_NONE) continue;

        auto it = counts.find(std::string(strText(parent.nameId)));
        if (it == counts.end() || it->second == 0) continue;

        for (uint32_t ord = 0; ord < parent.subcmdSiz; ++ord)
        {
            const uint32_t childIdx = parent.subcmdOff + ord;
            if (childIdx >= baseNodes.size()) break;

            const CommandNode& child = baseNodes[childIdx];
            if (child.nameId == CommandNode::STR_NONE) continue;

            const std::string range = expandPortPlaceholder(strText(child.nameId), it->second);
            if (range.empty()) continue;

            patch.patchName(childIdx, child, range);
        }
    }
}

void CommandTree::build(const std::string& sourcePath, const std::string& binaryPath)
{
    std::vector<std::byte> flat = parser::flattenDir(sourcePath);

    const std::string tmpPath = binaryPath + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cli::tree::CommandTree::build: cannot open " + tmpPath);
        out.write(reinterpret_cast<const char*>(flat.data()),
                  static_cast<std::streamsize>(flat.size()));
        if (!out)
            throw std::runtime_error("cli::tree::CommandTree::build: short write on " + tmpPath);
    }

    if (std::rename(tmpPath.c_str(), binaryPath.c_str()) != 0)
    {
        std::remove(tmpPath.c_str());
        throw std::runtime_error("cli::tree::CommandTree::build: cannot rename onto " + binaryPath);
    }
}

void CommandTree::bindBase()
{
   if (storage.size() < sizeof(FileHeader))
       throw std::runtime_error("cli::tree::CommandTree: buffer too small for header");
    header = reinterpret_cast<const FileHeader*>(storage.data());
    if (header->magic != FileHeader::CT_MAGIC)
        throw std::runtime_error("cli::tree::CommandTree: bad magic (not a JsonIndex file)");
    if (header->version != FileHeader::CT_VERSION)
        throw std::runtime_error("cli::tree::CommandTree: unsupported version");

    // Registry ids are list positions, so a file written against a different
    // registry list would decode its ids as the wrong registries entirely.
    if (header->registryHash != config::REGISTRY_FIELD_HASH)
        throw std::runtime_error("cli::tree::CommandTree: registry list changed since this file was written");

    // [FileHeader][ModeEntryNode[]][CommandNode[]][StrRef[]][blob]
    const std::byte* modesPtr = storage.data() + header->headerSize;
    baseModes = std::span<const ModeEntryNode>(
        reinterpret_cast<const ModeEntryNode*>(modesPtr), header->modeCount);
    const std::byte* nodesPtr = modesPtr + header->modeCount * sizeof(ModeEntryNode);
    baseNodes = std::span<const CommandNode>(
        reinterpret_cast<const CommandNode*>(nodesPtr), header->nodeCount);
    const std::byte* strsPtr = nodesPtr + header->nodeCount * sizeof(CommandNode);
    baseStrs = std::span<const StrRef>(
        reinterpret_cast<const StrRef*>(strsPtr), header->strCount);
    const std::byte* blobPtr = strsPtr + header->strCount * sizeof(StrRef);
    baseBlob = std::span<const char>(
        reinterpret_cast<const char*>(blobPtr), header->blobSize);

    size_t total = static_cast<size_t>(header->headerSize)
                 + header->modeCount * sizeof(ModeEntryNode)
                 + header->nodeCount * sizeof(CommandNode)
                 + header->strCount * sizeof(StrRef)
                 + header->blobSize;
    if (storage.size() < total)
        throw std::runtime_error("cli::tree::CommandTree: buffer shorter than its header describes");

    for (const StrRef& r : baseStrs)
        if (static_cast<size_t>(r.off) + r.len > baseBlob.size())
            throw std::runtime_error("cli::tree::CommandTree: interned string out of blob bounds");
}

ModeEntry CommandTree::getMode(CliMode mode) const
{
    cli::ModePath path = cli::getPath(mode);
    size_t index;
    if (path.size() == 1)
        index = findMode(path[0]);
    else if (path.size() == 2)
        index = findMode(path[0], path[1]);
    else
        throw std::runtime_error("cli::tree::CommandTree: submode goes too deep");

    if (index >= baseModes.size())
        throw std::runtime_error("cli::tree::CommandTree: mode index out of range");

    return ModeEntry(this, static_cast<uint32_t>(index));
}

ModeEntry CommandTree::modeEntry(size_t index) const
{
    if (index >= baseModes.size())
        throw std::runtime_error("cli::tree::CommandTree: mode index out of range");

    return ModeEntry(this, static_cast<uint32_t>(index));
}

size_t CommandTree::modeCount() const
{
    return baseModes.size();
}

size_t CommandTree::findMode(const std::string_view modeName) const
{
    return findMode(modeName, {});
}

size_t CommandTree::findMode(const std::string_view modeName, std::string_view subName) const
{
    for (size_t i = 0; i < baseModes.size(); ++i)
    {
        ModeEntry e(this, static_cast<uint32_t>(i));
        if (e.name() == modeName && e.subName() == subName) return i;
    }
    return NPOS;
}

const ModeEntryNode& CommandTree::modeAt(uint32_t idx) const
{
    if (idx >= baseModes.size())
        throw std::runtime_error("cli::tree::CommandTree: mode index out of range");
    return baseModes[idx];
}

std::string_view CommandTree::strText(uint16_t id) const
{
    if (id == CommandNode::STR_NONE) return {};
    if (id >= TreePatch::ID_BIAS) return patch.text(id);

    if (id >= baseStrs.size())
        throw std::runtime_error("cli::tree::CommandTree: string id out of range");

    // bindBase already checked every entry against the blob.
    const StrRef& r = baseStrs[id];
    return std::string_view(baseBlob.data() + r.off, r.len);
}

const CommandNode& CommandTree::nodeAt(uint32_t idx) const
{
    if (idx >= baseNodes.size())
        throw std::runtime_error("cli::tree::CommandTree: node index out of range");

    if (!patch.empty())
        if (const CommandNode* patched = patch.find(idx)) return *patched;

    return baseNodes[idx];
}

uint32_t CommandTree::childIndex(const CommandNode& parent, uint32_t ordinal) const
{
    // Children sit in one contiguous block, so an unchecked ordinal would read
    // a real but unrelated node rather than fail.
    if (ordinal >= parent.subcmdSiz)
        throw std::runtime_error("cli::tree::CommandTree: subcommand index out of range");
    return parent.subcmdOff + ordinal;
}
}
