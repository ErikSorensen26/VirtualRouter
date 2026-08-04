/**
 * @file Executor.hpp
 * @brief Turns a resolved command into the config write it stands for.
 *
 * Parsing answers "which command is this, and what are its arguments". Running
 * it is a separate question, and this is the only place that answers it: given
 * the node the parse landed on plus that command's value tokens, find what is
 * bound to the node and call it.
 *
 * Two things can be bound, and both go through the same configId. Most commands
 * write a field. A few enter a mode, and those resolve the same binding to a
 * child registry rather than to a value -- `interface Vlan 10` looks 10 up in the
 * owned list the field names, and the instance it finds becomes the context the
 * next line writes to.
 *
 * The split matters because parsing is entangled with the terminal -- help
 * listings, tab completion, caret markers, pagination -- and running a command
 * is not. Nothing here prints, reads input, or knows a console exists. An
 * @ref ExecRequest carries only what execution needs, so the five parse outcomes
 * that exist purely to be *displayed* never reach this far.
 */

#ifndef CLI_EXECUTOR_HPP
#define CLI_EXECUTOR_HPP

#include <span>

#include <algorithm>
#include <vector>

#include "cli/execution/ExecutorUtils.hpp"
#include "cli/execution/TupleStaging.hpp"
#include "cli/session/Token.hpp"
#include "cli/modes/Context.hpp"
#include "cli/session/TreeNavigator.hpp"
#include "configs/RegistryTable.hpp"
#include "cli/tree/nodes/Command.h"
#include "configs/FieldAccessor.hpp"
#include "configs/RegistryDefaultTable.hpp"

namespace cli::execution
{


/**
 * @brief Runs resolved commands against the live config.
 *
 * Holds no state between lines: the mode stack and the context outlive it and
 * carry everything a command needs, so one instance serves the whole session.
 *
 * Within a line it walks the tokens front to back, because the binding rides
 * the keyword token rather than the value that follows it. Four things can
 * happen per token -- enter a mode, stage a tuple member, write a field, or
 * nothing at all for a keyword that only routed the parse.
 */
class Executor
{
public:

    Executor(TreeNavigator& n, ContextBase& c)
        : nav(n), ctx(c)
    {}

    /**
     * @brief Runs one resolved command against the active registry.
     *
     * The registry is not a parameter: @p ctx already points at the live config for
     * the current mode, and the mode stack repoints it on every transition. Applying
     * a field therefore needs nothing the context does not already carry.
     *
     * The negate/default flags are copied onto the context rather than passed along,
     * because the appliers read them from there -- a bool field toggles off, and a
     * `default` restores the registry default -- and the context is what they get.
     *
     * @param req  The command to run and its arguments.
     * @param ctx  Execution context: target registry plus negate/default state.
     * @param nav  Mode stack, for a command that enters one.
     * @return What happened; see @ref ExecStatus.
     */
    bool execute(std::span<Token> tokens)
    {
        std::vector<execution::TupleMember> staged;

        for (size_t i = 0; i < tokens.size(); )
        {
            const size_t start = i;

            if (tokens[i].modeFlagged())
            {
                i = utils::runEnd(tokens, i, &Token::modeFlagged);
                if (!handleModeChange(tokens.subspan(0, i))) return false;
            }
            else if (tokens[i].modeExit())
            {
                ++i;
                if (!handleModeExit(tokens[start])) return false;
            }
            else if (tokens[i].tupChange())
            {
                i = utils::runEnd(tokens, i, &Token::tupChange);
                execution::stageTupleMembers(tokens.subspan(start, i - start), staged);
            }
            else if (tokens[i].hasNode())
            {
                ++i;
                if (!handleValueChange(tokens[start])) return false;
            }
            else
            {
                // A bare keyword: it routed the parse here but writes nothing.
                ++i;
            }
        }

        return execution::commitTuples(ctx, staged);
    }

private:

    /**
     * @brief Resolves the bound field and enters the mode it leads to.
     *
     * The field is what says which registry the new mode edits, so it is found
     * first: an owned list needs a key to pick an instance, a container is a
     * single scope and needs none. Which of the two it is decides whether a key
     * token is looked for at all, so the field leads and the key follows.
     *
     * The *last* bound token in the run is the one that names the field, not
     * the first. A placeholder expands to a keyword and its value -- Vlan then
     * 10 -- and the binding rides the deepest node the parse reached, so an
     * earlier token in the same run is a step on the way rather than the
     * destination.
     *
     * Entering is insert-or-return, matching the config path -- `interface Vlan
     * 10` enters Vlan 10 whether or not it has been configured before.
     */
    bool handleModeChange(std::span<Token> toks)
    {
        const Token* binding = nullptr;
        for (Token& t : toks)
            if (t.hasNode()) binding = &t;

        if (!binding) return false;

        const tree::CommandNode& bound = binding->node.node();

        bool ok = false;
        utils::visitBound(ctx, bound, [&](auto&& field)
        {
            // A container kind is visited as itself; every other kind arrives
            // wrapped in an accessor, which is what carries the nested Field.
            using Visited = std::remove_cvref_t<decltype(field)>;

            if constexpr (config::IsRefContainer<Visited>)
            {
                // A single scope, so nothing to key on; any token on the line
                // belongs to the command rather than to the lookup.
                ok = nav.changeMode(utils::modeOf(bound), field.get());
            }
            else if constexpr (requires { typename Visited::Field; })
            {
                using Field = typename Visited::Field;

                if constexpr (config::IsOwnedListField<Field>)
                {
                    using Key = typename Field::key;

                    Key key{};
                    if (!utils::resolveKey<Key>(toks, key)) return;

                    ok = nav.changeMode(utils::modeOf(bound), field.emplaceBack(key));
                }
            }
        });

        return ok;
    }

    /**
     * @brief Leaves the current mode, or every mode, without writing anything.
     *
     * The mirror of @ref handleModeChange and deliberately much less work: an
     * exit resolves no field, because where it lands is whatever the navigation
     * stack was already holding rather than something the command names.
     *
     * `end` unwinds to the bottom where `exit` steps back one. Which of the two
     * this is comes from the command's name rather than a flag: both are the
     * same kind of node to the tree, and the grammar has one spelling for each.
     */
    bool handleModeExit(Token& t)
    {
        if (t.node.name() != "end") return nav.popMode();

        while (isConfigurationMode(nav.getMode()) && nav.popMode()) {}

        return true;
    }

    /**
     * @brief Writes one token into the field its node binds.
     */
    bool handleValueChange(Token& value)
    {
        const tree::CommandNode& bound = value.node.node();

        bool ok = false;
        utils::visitBound(ctx, bound, [&](auto&& accessor)
        {
            using Visited = std::remove_cvref_t<decltype(accessor)>;

            // Container kinds hold a scope rather than a value, and are visited
            // as themselves, so they carry no Field and nothing to write.
            if constexpr (requires { typename Visited::Field; })
            {
                using Field = typename Visited::Field;

                if constexpr (utils::TokenWritable<Field>)
                {
                    using Value = typename Field::type;

                    if constexpr (std::is_enum_v<Value>)
                    {
                        if (bound.hasEnumChange())
                        {
                            // `no duplex full` restores the default rather than
                            // writing FULL, same as any other negated field.
                            if (!utils::handleValueReset(accessor, ctx))
                                accessor.set(static_cast<Value>(bound.configExt));
                            ok = true;
                        }
                        else
                        {
                            ok = utils::setFieldValue(accessor, ctx, &value);
                        }
                    }
                    else if constexpr (std::is_same_v<bool, Value>)
                    {
                        utils::setToggleValue(accessor, ctx);
                        ok = true;
                    }
                    else
                    {
                        ok = utils::setFieldValue(accessor, ctx, &value);
                    }
                }
            }
        });

        return ok;
    }

    TreeNavigator& nav;
    ContextBase& ctx;
};
}

#endif // CLI_EXECUTOR_HPP
