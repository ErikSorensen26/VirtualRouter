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

#include "cli/execution/BitMapFlags.hpp"
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
 * the keyword token rather than the value that follows it. Each step consumes
 * one run and dispatches on what opened it: entering a mode, leaving one,
 * rescoping the rest of the line, staging a tuple member, applying a run of
 * bitmap flags, writing a field, or nothing at all for a keyword that only
 * routed the parse.
 *
 * Staged tuple members are the exception to running as it walks: a tuple entry
 * is assembled from members named across the line and cannot be written until
 * the line ends, so those are collected and committed together at the end.
 */
class Executor
{
public:

    /**
     * @brief A write scope: the registry pointer and which registry it is.
     *
     * The two travel together everywhere the pointer is not used on the spot,
     * because a void* on its own cannot be checked against the registry a
     * command claims -- which is the whole point of carrying the id.
     */
    struct Scope
    {
        void* ptr = nullptr;
        uint16_t registry = ContextBase::NO_REGISTRY;
    };

    /**
     * @brief Binds an executor to the session state it runs against.
     *
     * Both are held by reference and outlive it; see the class doc on why one
     * instance serves the whole session.
     *
     * @param n Mode stack, for commands that enter or leave a mode.
     * @param c Execution context, pointing at the registry being written.
     */
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
     * Deferred values are resolved before the walk begins, since a command that
     * holds its value under a key may be read before the command supplying it.
     *
     * @param tokens The matched line: every token the parse produced, each
     *               carrying the tree node it resolved to.
     * @return False as soon as a step fails, which abandons the rest of the
     *         line; writes already applied are kept.
     */
    bool execute(std::span<Token> tokens)
    {
        std::vector<Token*> staged;

        RegistryScope scope(ctx);

        std::vector<Token> toks;
        toks.reserve(tokens.size());

        for (size_t i = 0; i < tokens.size(); i++)
        {
            Token& tok = tokens[i];

            if (tok.resolver()) continue;

            toks.push_back(tok);

            if (!tok.deferred()) continue;

            const uint8_t deferKey = tok.node.node().deferKey();
            for (size_t x = 0; x < tokens.size(); x++)
            {
                Token& r = tokens[x];
                if (r.resolver() && r.node.node().deferKey() == deferKey)
                {
                    toks.push_back(r);
                    break;
                }
            }
        }

        const std::span<Token> line(toks);

        for (size_t i = 0; i < line.size(); )
        {
            const size_t start = i;
            Token& tok = line[i];

            if (tok.modeFlagged())
            {
                i = utils::nextBound(line, i, &Token::modeFlagged);
                if (!handleModeChange(line.subspan(0, i), scope.release())) return false;
            }
            else if (tok.registryFlagged())
            {
                i = utils::nextBound(line, i, &Token::registryFlagged);
                scope.arm();
                if (!handleRegistryChange(line.subspan(start, i - start))) return false;
            }
            else if (tok.modeExit())
            {
                ++i;
                scope.disarm();
                if (!handleModeExit(line[start])) return false;
            }
            else if (tok.tupChange())
            {
                ++i;
                staged.push_back(&tok);
            }
            else if (tok.bitMapFlag())
            {
                i = execution::bitMapRunEnd(line, i);
                if (!handleBitMapFlags(line.subspan(start, i - start))) return false;
            }
            else if (tok.hasNode())
            {
                i = utils::nextBound(line, i, &Token::hasNode);
                i = utils::nextSegment(line, start, i);
                if (!handleValueChange(line.subspan(start, i - start))) return false;
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
     *
     * @p retTo is where `exit` comes back to. It is passed in rather than read
     * from the context because a rescope earlier on the same line has already
     * moved ctx.ctx, and the binding below is resolved against that moved
     * pointer -- see @ref RegistryScope::release.
     */
    bool handleModeChange(std::span<Token> toks, Scope retTo)
    {
        const Token* binding = nullptr;
        for (Token& t : toks)
            if (t.hasNode()) binding = &t;

        if (!binding) return false;

        const tree::CommandNode& bound = binding->node.node();

        bool ok = false;
        utils::visitBound(ctx, bound, [&](auto&& field)
        {
            using Visited = std::remove_cvref_t<decltype(field)>;

            if constexpr (config::IsRefContainer<Visited>)
            {
                auto& entered = field.get();
                ok = nav.changeMode(
                    utils::modeOf(bound), static_cast<void*>(&entered),
                    TreeNavigator::registryIdOf<std::remove_reference_t<decltype(entered)>>(),
                    retTo.ptr, retTo.registry);
            }
            else if constexpr (requires { typename Visited::Field; })
            {
                using Field = typename Visited::Field;

                if constexpr (config::IsOwnedListField<Field>)
                {
                    using Key = typename Field::key;

                    Key key{};
                    if (!utils::resolveKey<Key>(toks, key)) return;

                    auto& entered = field.emplaceBack(key, binding->node.nodeIndex());
                    ok = nav.changeMode(
                        utils::modeOf(bound), static_cast<void*>(&entered),
                        TreeNavigator::registryIdOf<std::remove_reference_t<decltype(entered)>>(),
                        retTo.ptr, retTo.registry);
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
     * @brief Restores the context's write scope when the line ends.
     *
     * A rescope is confined to the command that asked for it, so it cannot be
     * left to the next line to undo: an early return out of the run loop is a
     * normal outcome, and the session would go on writing to a sub-registry it
     * never entered.
     *
     * Armed rather than always-on, because a mode change moves the very same
     * pointer and is *supposed* to outlive the line. Restoring unconditionally
     * would undo every `interface Gi1` the moment it finished.
     */
    class RegistryScope
    {
    public:
        explicit RegistryScope(ContextBase& c)
            : ctx(c), entry(c.ctx), entryReg(c.ctxRegistry) {}

        ~RegistryScope() { if (armed) ctx.rescope(entry, entryReg); }

        /// @brief Called before rescoping, so a half-applied one still reverts.
        void arm() { armed = true; }

        /// @brief Gives up the saved scope, for a command that moves it for good.
        void disarm() { armed = false; }

        /**
         * @brief Hands back the pre-rescope scope and stops tracking.
         *
         * A mode change on a rescoped line needs both pointers, for different
         * things. Its binding lives in the registry the rescope moved to --
         * `router eigrp 1` reads ROUTER_EIGRP_V4 out of the VRF that `eigrp`
         * selected -- so ctx.ctx has to still be the rescoped one when the
         * field is resolved. But the frame `exit` comes back to has to be the
         * pointer the line started on, or popMode restores a registry whose type
         * no longer matches the mode it claims.
         *
         * So the rescope is not undone here, only surrendered: the caller is
         * taking over responsibility for the saved pointer and passes it to the
         * mode change to be pushed in ctx.ctx's place.
         *
         * @return The write scope as it stood before any rescope on this line.
         */
        Scope release()
        {
            armed = false;
            return {entry, entryReg};
        }

        RegistryScope(const RegistryScope&) = delete;
        RegistryScope& operator=(const RegistryScope&) = delete;

    private:
        ContextBase& ctx;
        void* entry;
        uint16_t entryReg;
        bool armed = false;
    };

    /**
     * @brief Points the rest of the line at the registry the run's field names.
     *
     * The same resolution a mode change does -- a container is one scope, an
     * owned list needs a key to pick an instance -- but it stops there. The mode
     * stack and the grammar cursor are untouched, so the session stays where it
     * was standing and only the write scope moves.
     */
    bool handleRegistryChange(std::span<Token> toks)
    {
        const Token* binding = nullptr;
        for (Token& t : toks)
            if (t.hasNode()) binding = &t;

        if (!binding) return false;

        const tree::CommandNode& bound = binding->node.node();

        bool ok = false;
        utils::visitBound(ctx, bound, [&](auto&& field)
        {
            using Visited = std::remove_cvref_t<decltype(field)>;

            if constexpr (config::IsRefContainer<Visited>)
            {
                auto& moved = field.get();
                ctx.rescope(static_cast<void*>(&moved),
                             TreeNavigator::registryIdOf<std::remove_reference_t<decltype(moved)>>());
                ok = true;
            }
            else if constexpr (requires { typename Visited::Field; })
            {
                using Field = typename Visited::Field;

                if constexpr (config::IsOwnedListField<Field>)
                {
                    using Key = typename Field::key;

                    Key key{};
                    if (!utils::resolveKey<Key>(toks, key)
                        && !std::is_default_constructible_v<Key>)
                        return;

                    auto& moved = field.emplaceBack(key, binding->node.nodeIndex());
                    ctx.rescope(static_cast<void*>(&moved),
                                 TreeNavigator::registryIdOf<std::remove_reference_t<decltype(moved)>>());
                    ok = true;
                }
            }
        });

        return ok;
    }

    /**
     * @brief Applies a run of flags to the one bitmap field they share.
     *
     * The run is resolved through its first token, since every token in it
     * binds the same field by construction; the rest contribute only their
     * members.
     */
    bool handleBitMapFlags(std::span<Token> toks)
    {
        const tree::CommandNode& bound = toks.front().node.node();

        bool ok = false;
        utils::visitBound(ctx, bound, [&](auto&& accessor)
        {
            using Visited = std::remove_cvref_t<decltype(accessor)>;

            if constexpr (requires { typename Visited::Field; })
            {
                if constexpr (utils::TokenWritable<typename Visited::Field>)
                    ok = execution::applyBitMapFlags(accessor, ctx, toks);
            }
        });

        return ok;
    }

    /**
     * @brief Writes a run of tokens into the field its head node binds.
     *
     * Normally that is one token. A field whose type is spelled with two words,
     * such as an address and its mask, binds only on the first and reads the
     * second from the run behind it -- which token count a field wants is a
     * property of its type, so only the visit below can decide it.
     */
    bool handleValueChange(std::span<Token> toks)
    {
        Token& value = toks[0];
        Token* extra = toks.size() > 1 ? &toks[1] : nullptr;

        const tree::CommandNode& bound = value.node.node();

        bool ok = false;
        utils::visitBound(ctx, bound, [&](auto&& accessor)
        {
            using Visited = std::remove_cvref_t<decltype(accessor)>;

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
                            if (!utils::handleValueReset(accessor, ctx))
                                accessor.set(static_cast<Value>(bound.configExt),
                                             value.node.nodeIndex());
                            ok = true;
                        }
                        else
                        {
                            ok = utils::setFieldValue(accessor, ctx, &value);
                        }
                    }
                    else if constexpr (std::is_same_v<bool, Value>)
                    {
                        utils::setToggleValue(accessor, ctx, value.node.nodeIndex());
                        ok = true;
                    }
                    else if constexpr (utils::DoubleValued<Value>)
                    {
                        ok = utils::setDoubleFieldValue(accessor, ctx, &value, extra);
                    }
                    else
                    {
                        ok = utils::setFieldValue(accessor, ctx, &value);
                    }
                }
                else if constexpr (utils::ScalarListWritable<Field>)
                {
                    using Node = typename Field::element;

                    Node entry{};
                    bool built = false;

                    if constexpr (utils::DoubleValued<Node>)
                        built = utils::setDoubleTupleElement(entry, &value, extra);
                    else
                        built = utils::setTupleElement(entry, &value);

                    if (built)
                        ok = utils::setListEntry(accessor, ctx, entry,
                                                 value.node.nodeIndex());
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
