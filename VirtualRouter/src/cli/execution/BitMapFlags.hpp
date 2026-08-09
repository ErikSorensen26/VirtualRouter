/**
 * @file BitMapFlags.hpp
 * @brief Applies a command's flag list to one EnumBitMap field.
 *
 * An enum-valued field takes one member and keeps it, so each token can be
 * written as it is parsed. A bitmap field cannot work that way: `eigrp stub
 * connected summary` names two members of the *same* field, and writing each
 * as a value would leave only the last one set.
 *
 * So the flags are collected into a run and applied together. The run is the
 * whole of the field's setting -- re-running the command with fewer flags
 * clears the ones left off, the same way re-running any command drops the
 * clauses it omits -- so the bitmap is built from scratch and stored once
 * rather than OR-ed into whatever was there before.
 *
 * A run is delimited by the field, not by adjacency alone: two bitmap fields
 * mentioned on one line are two runs, each replacing its own field.
 */

#ifndef CLI_BIT_MAP_FLAGS_HPP
#define CLI_BIT_MAP_FLAGS_HPP

#include <span>
#include <type_traits>

#include <EnumBitMap.hpp>

#include "cli/execution/ExecutorUtils.hpp"
#include "cli/modes/Context.hpp"
#include "cli/session/Token.hpp"
#include "cli/tree/nodes/Command.h"

namespace cli::execution
{

/**
 * @brief End of the run of bitmap flags at `i` that share one field.
 *
 * Unlike the general runEnd this cannot stop at the first bound token: every
 * flag in the run binds a field, since that is what makes it a flag. What ends
 * the run is a token binding a *different* field, or one that is not a flag.
 */
inline size_t bitMapRunEnd(std::span<Token*> toks, size_t i)
{
    const uint32_t field = toks[i]->node.node().configId;

    for (++i; i < toks.size(); ++i)
        if (toks[i] && (!toks[i]->bitMapFlag() || toks[i]->node.node().configId != field)) break;

    return i;
}

/**
 * @brief Sets the members a run names on the bitmap field they share.
 *
 * Negate and default are handled first and for the run as a whole: `no eigrp
 * stub connected` clears the field rather than clearing the one bit, matching
 * how negation treats every other field.
 *
 * The run records the node that opened it as the writing command, since the run
 * is one command however many flags it spells. Recording a later flag instead
 * would name a node that cannot reproduce the rest of them.
 */
template <typename Accessor>
bool applyBitMapFlags(Accessor accessor, cli::ContextBase& ctx, std::span<Token*> toks)
{
    using Value = std::remove_cvref_t<typename Accessor::Field::type>;

    if constexpr (types::isEnumBitMapV<Value>)
    {
        if (utils::handleValueReset(accessor, ctx)) return true;

        Value bits;
        for (Token* t : toks)
            bits.set(static_cast<typename Value::Enum>(t->node.node().configExt));

        accessor.set(bits, toks.empty() ? config::NO_COMMAND_INDEX
                                        : toks.front()->node.nodeIndex());
        return true;
    }
    else
    {
        return false;
    }
}
}

#endif // CLI_BIT_MAP_FLAGS_HPP
