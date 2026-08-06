// ModeEntry.cpp

#include "ModeEntry.h"
#include "Command.h"
#include "../CommandTree.h"

namespace cli::tree
{
ModeEntry::ModeEntry(const CommandTree* tree, uint32_t index)
    : tree(tree), index(index)
{}

const ModeEntryNode& ModeEntry::node() const
{
    return tree->modeAt(index);
}

std::string_view ModeEntry::name() const
{
    return tree->strText(node().nameId);
}

std::string_view ModeEntry::subName() const
{
    return tree->strText(node().subId);
}

std::string_view ModeEntry::prompt() const
{
    return tree->strText(node().promptId);
}

bool ModeEntry::hasSubMode() const
{
    return node().subId != ModeEntryNode::STR_NONE;
}

bool ModeEntry::hasRegistry() const
{
    return node().registryId != ModeEntryNode::REGISTRY_NONE;
}

uint16_t ModeEntry::registryId() const
{
    return node().registryId;
}

size_t ModeEntry::size() const
{
    return node().cmdSiz;
}

Command ModeEntry::commands() const
{
    return Command(tree, node().cmdOff);
}
}
