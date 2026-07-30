// ModeEntry.cpp

#include "ModeEntry.h"
#include "Command.h"
#include "CommandTree.h"

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
    const ModeEntryNode& e = node();
    if (e.modeSiz == 0) return {};
    return tree->blobText(e.infoOff, e.modeSiz);
}

std::string_view ModeEntry::subName() const
{
    const ModeEntryNode& e = node();
    if (e.subSiz == 0) return {};
    return tree->blobText(e.infoOff + e.modeSiz, e.subSiz);
}

bool ModeEntry::hasSubMode() const
{
    return node().subSiz != 0;
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
