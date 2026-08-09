// Command.cpp

#include "Command.h"
#include "../CommandTree.h"

constexpr std::string_view CARRIAGE_RETURN = "<cr>";

namespace cli::tree
{
Command::Command(const CommandTree* tree, uint32_t index)
    : tree(tree), index(index)
{}

bool Command::hasProp(CommandNode::Property prop) const
{
    if (!tree) return false;
    return resolveNode().has(prop);
}

bool Command::hasExclusiveCR() const
{
    return hasCarriageReturn() && size() == 1;
}

bool Command::hasCarriageReturn() const
{
    if (size() == 0) return true;
    return find(CARRIAGE_RETURN) != NPOS;
}

std::string_view Command::name() const
{
    if (!tree) return CARRIAGE_RETURN;
    return tree->strText(resolveNode().nameId);
}

std::string_view Command::desc() const
{
    if (!tree) return {};
    return tree->strText(resolveNode().descId);
}

std::string_view Command::recurseExcludeCsv() const
{
    if (!tree) return {};
    return tree->strText(resolveNode().recurseExcludeId);
}

size_t Command::size() const
{
    if (!tree) return 0;
    const CommandNode& n = resolveNode();
    return n.subcmdSiz;
}

Command Command::at(size_t i) const
{
    if (!tree) return {};
    return Command(tree, tree->childIndex(resolveNode(), static_cast<uint32_t>(i)));
}

Command Command::parent() const
{
    if (!tree) return {};
    const CommandNode& n = resolveNode();
    if (!n.hasParent()) return {};
    return Command(tree, n.parentIndex);
}

size_t Command::find(std::string_view childName) const
{
    if (!tree) return NPOS;
    size_t count = size();
    for (size_t i = 0; i < count; ++i)
        if (at(i).name() == childName) return i;
    return NPOS;
}

const CommandNode& Command::resolveNode() const
{
    return tree->nodeAt(index);
}
}
