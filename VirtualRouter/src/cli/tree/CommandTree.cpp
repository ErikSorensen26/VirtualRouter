// CommandTree.cpp

#include "CommandTree.h"
#include "FileHeader.hpp"
#include <Json.hpp>
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

CommandTree::CommandTree(const std::string& jsonPath, const std::string& binaryPath)
{
    try
    {
        storage = Storage::mapFile(binaryPath);
        bindBase();
        return;
    }
    catch (const std::runtime_error&)
    {
        storage = Storage();
    }

    build(jsonPath, binaryPath);
    storage = Storage::mapFile(binaryPath);
    bindBase();
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
        if (parent.subcmdSiz == 0 || parent.nameSiz == 0) continue;

        auto it = counts.find(std::string(blobText(parent.infoOff, parent.nameSiz)));
        if (it == counts.end() || it->second == 0) continue;

        for (uint32_t ord = 0; ord < parent.subcmdSiz; ++ord)
        {
            const uint32_t childIdx = parent.subcmdOff + ord;
            if (childIdx >= baseNodes.size()) break;

            const CommandNode& child = baseNodes[childIdx];
            if (child.nameSiz == 0) continue;

            const std::string_view name = blobText(child.infoOff, child.nameSiz);
            const std::string range = expandPortPlaceholder(name, it->second);
            if (range.empty()) continue;

            patch.patchName(childIdx, child, range,
                            blobText(child.infoOff + child.nameSiz, child.descSiz));
        }
    }
}

void CommandTree::build(const std::string& jsonPath, const std::string& binaryPath)
{
    utils::json::JsonNode dom = utils::json::load(jsonPath);
    std::vector<std::byte> flat = parser::flattenCmds(dom);

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

    // [FileHeader][ModeEntryNode[]][CommandNode[]][blob]
    const std::byte* modesPtr = storage.data() + header->headerSize;
    baseModes = std::span<const ModeEntryNode>(
        reinterpret_cast<const ModeEntryNode*>(modesPtr), header->modeCount);
    const std::byte* nodesPtr = modesPtr + header->modeCount * sizeof(ModeEntryNode);
    baseNodes = std::span<const CommandNode>(
        reinterpret_cast<const CommandNode*>(nodesPtr), header->nodeCount);
    const std::byte* blobPtr = nodesPtr + header->nodeCount * sizeof(CommandNode);
    baseBlob = std::span<const char>(
        reinterpret_cast<const char*>(blobPtr), header->blobSize);

    size_t total = static_cast<size_t>(header->headerSize)
                 + header->modeCount * sizeof(ModeEntryNode)
                 + header->nodeCount * sizeof(CommandNode)
                 + header->blobSize;
    if (storage.size() < total)
        throw std::runtime_error("cli::tree::CommandTree: buffer shorter than its header describes");
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

std::string_view CommandTree::blobText(uint32_t off, uint32_t len) const
{
    if (off >= TreePatch::OFFSET_BIAS)
    {
        const std::string_view patched = patch.text();
        const size_t start = off - TreePatch::OFFSET_BIAS;
        if (start + len > patched.size())
            throw std::runtime_error("cli::tree::CommandTree: patch range out of bounds");
        return patched.substr(start, len);
    }

    if (static_cast<size_t>(off) + len > baseBlob.size())
        throw std::runtime_error("cli::tree::CommandTree: blob range out of bounds");
    return std::string_view(baseBlob.data() + off, len);
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
