/**
 * @file Executor.hpp
 * @brief Turns a resolved command into the config write it stands for.
 * @ingroup CLI_PARSER
 *
 * Parsing answers "which command is this, and what are its arguments". Running
 * it is a separate question, and this is the only place that answers it: given
 * the node the parse landed on plus that command's value tokens, find what is
 * bound to the node and call it.
 *
 * Three things can be bound, and all go through the same configId. Most commands
 * write a field. A few enter a mode, and those resolve the same binding to a
 * child registry rather than to a value -- `interface Vlan 10` looks 10 up in the
 * owned list the field names, and the instance it finds becomes the context the
 * next line writes to. A rescope resolves exactly as a mode change does but stops
 * there, moving the write scope for the rest of the line while the session stays
 * where it was standing.
 *
 * The split matters because parsing is entangled with the terminal -- help
 * listings, tab completion, caret markers, pagination -- and running a command
 * is not. Nothing here prints, reads input, or knows a console exists. The
 * matched tokens carry only what execution needs, so the parse outcomes that
 * exist purely to be *displayed* never reach this far.
 */

#ifndef CLI_EXECUTOR_HPP
#define CLI_EXECUTOR_HPP

#include <span>
#include <cstdio>

#include <algorithm>
#include <vector>

#include "cli/execution/BitMapFlags.hpp"
#include "cli/execution/ExecutorUtils.hpp"
#include "cli/execution/TupleStaging.hpp"
#include "cli/session/Token.hpp"
#include "cli/modes/Context.hpp"
#include "cli/session/TreeNavigator.hpp"
#include "cli/tree/nodes/Command.h"

namespace cli::execution
{


/**
 * @brief Runs resolved commands against the live config.
 *
 * Holds no state between lines: the mode stack and the context outlive it and
 * carry everything a command needs, so one instance serves the whole session.
 *
 * A line is not run in the order it was typed. It is first grouped into the
 * scopes its commands write to -- see @ref orderByScope -- so that what a
 * command targets does not depend on where the phrasing happened to put a
 * rescope. Within a block the typed order stands, because the binding rides the
 * keyword token rather than the value that follows it.
 *
 * The walk then consumes one run at a time and dispatches on what opened it:
 * entering a mode, leaving one, rescoping the rest of the line, staging a tuple
 * member, applying a run of bitmap flags, writing a field, or nothing at all
 * for a keyword that only routed the parse.
 *
 * Staged tuple members are the exception to running as it walks: a tuple entry
 * is assembled from members named across a block and cannot be written until
 * that block ends, so those are collected and committed at every boundary that
 * moves the write scope, and once more when the line runs out.
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
     * @brief Which scope of a line a token is written in.
     *
     * Rescopes are numbered in the order the line names them, since each one
     * resolves its field out of the scope before it and so has to run in that
     * order. A mode change is not part of that sequence -- it is last whatever
     * else the line did -- so it says so on its own field rather than by
     * holding a reserved index, which would make it the same value as a
     * rescope that counted far enough.
     *
     * This is the sort comparator, so @ref operator< has to stay a strict weak
     * ordering: compare the fields in one fixed order, most significant first,
     * and let a new dimension join that sequence rather than cut across it.
     */
    struct Block
    {
        bool isMode = false;
        uint32_t index = 0;

        bool operator<(const Block& o) const
        {
            if (isMode != o.isMode) return !isMode;
            return index < o.index;
        }
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
     * The line is then grouped by write scope, so the run loop below sees the
     * blocks in the order they have to run rather than the order they were
     * typed -- @ref orderByScope has the why.
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

        std::vector<Token*> toks;
        toks.reserve(tokens.size());

        std::vector<Token*> tail;   // deferred words that index at the end of the line

        for (size_t i = 0; i < tokens.size(); i++)
        {
            Token& tok = tokens[i];

            if (tok.deferred()
                && (tok.registryFlagged() || tok.modeFlagged()
                    || tok.bitMapFlag() || tok.tupChange()))
            {
                toks.push_back(&tok);

                const uint16_t deferKey = tok.node.node().deferKey();
                for (size_t x = tokens.size(); x-- > 0; )
                {
                    Token& r = tokens[x];

                    if (r.resolver() && r.node.node().deferKey() == deferKey)
                    {
                        if (!mergeDeferredResolver(tok, r)) toks.push_back(&r);
                        break;
                    }
                }
                continue;
            }

            if (tok.resolver()) continue;

            if (!tok.deferred())
            {
                toks.push_back(&tok);
                continue;
            }

            // A resolver never runs on its own: it either merges into the
            // deferred word that answers it or, on a disagreement, runs
            // alongside it. Both happen below; nothing here pushes it.
            if (tok.resolver()) continue;

            if (!tok.deferred())
            {
                toks.push_back(&tok);
                continue;
            }

            // An inert deferred word -- `area 5` before its `<cr>` -- only
            // indexes once the resolver in front of it agrees the word was
            // meant. The resolver pairs with the nearest deferred word in front
            // of it, so a later spelling of the same key supersedes an earlier
            // one, and a word the resolver passes over is dropped rather than
            // run. The write lands at the end of the line, behind the value
            // words, so they still go to the registry the line was typed in.
            const uint16_t deferKey = tok.node.node().deferKey();
            for (size_t x = i + 1; x < tokens.size(); x++)
            {
                Token& r = tokens[x];
                if (!r.resolver() || r.node.node().deferKey() != deferKey) continue;

                bool claimed = false;
                for (size_t y = i + 1; y < x; y++)
                {
                    if (tokens[y].deferred() && tokens[y].node.node().deferKey() == deferKey)
                    {
                        claimed = true;
                        break;
                    }
                }
                if (claimed) break;

                if (!mergeDeferredResolver(tok, r))
                {
                    // Two different fields on one word: each writes its own.
                    toks.push_back(&tok);
                    toks.push_back(&r);
                }
                else if (!r.node.node().hasConfig())
                {
                    // The resolver supplied nothing of its own, so the deferred
                    // word is the one that indexes.
                    tok.deferredWrite = true;
                    tail.push_back(&tok);
                }
                else
                {
                    // The resolver named the field: the merged word writes
                    // where it was typed.
                    toks.push_back(&tok);
                }
                break;
            }
        }

        toks.insert(toks.end(), tail.begin(), tail.end());

        orderByScope(toks);

        for (size_t i = 0; i < toks.size(); )
        {
            const size_t start = i;
            Token& tok = *toks[i];

            if (tok.modeFlagged())
            {
                if (!commitStaged(staged)) return false;

                i = utils::nextBound(toks, i, &Token::modeFlagged);

                if (!handleModeChange(std::span<Token*>(toks.data() + start, i - start),
                                      scope.release())) return false;
            }
            else if (tok.registryFlagged())
            {
                if (!commitStaged(staged)) return false;

                i = utils::nextBound(toks, i, &Token::registryFlagged);
                scope.arm();
                if (!handleRegistryChange(std::span<Token*>(toks.data() + start, i - start))) return false;
            }
            else if (tok.deferredWrite)
            {
                if (!commitStaged(staged)) return false;

                ++i;
                scope.arm();
                if (!handleRegistryChange(std::span<Token*>(toks.data() + start, i - start))) return false;
            }
            else if (tok.modeExit())
            {
                if (!commitStaged(staged)) return false;

                ++i;
                scope.disarm();
                if (!handleModeExit(toks[start])) return false;
            }
            else if (tok.tupChange())
            {
                ++i;
                staged.push_back(&tok);
            }
            else if (tok.bitMapFlag())
            {
                i = execution::bitMapRunEnd(toks, i);
                if (!handleBitMapFlags(std::span<Token*>(toks.data() + start, i - start))) return false;
            }
            else if (tok.hasNode())
            {
                i = utils::nextBound(toks, i, &Token::hasNode);
                i = utils::nextSegment(toks, start, i);
                if (!handleValueChange(std::span<Token*>(toks.data() + start, i - start))) return false;
            }
            else
            {
                // A bare keyword: it routed the parse here but writes nothing.
                ++i;
            }
        }

        // The last block ends with the line rather than at a command, so it has
        // no boundary of its own to commit at.
        return commitStaged(staged);
    }

private:

    /**
     * @brief Points a deferred token at whichever of it or its resolver is the
     * more complete command, so only one of the two runs.
     *
     * A deferred word and its resolver are two halves of one command split
     * across the line -- `area 5` (deferred, holds the key) and the `<cr>` or
     * literal that eventually names the value (resolver). Config, enum,
     * pattern and value are each checked independently: whichever side is the
     * only one to carry a given one of the four contributes it, and where both
     * sides carry the same one it has to agree or there is nothing to choose
     * between them.
     *
     * @ref Token::node is a cursor into the flattened tree, so it can only
     * ever point at one of the two existing nodes -- never a synthesis of
     * both. The four checks below are therefore run to decide whether one
     * side is a strict superset of the other (has everything the other has,
     * plus at least one thing it does not), which is the only shape a single
     * cursor can stand in for both. Any actual disagreement -- two different
     * config fields, two different enum members, two different pattern
     * classes, two different literal words -- refuses the merge outright
     * rather than guessing which one the grammar meant.
     *
     * @return True when @p tok now stands for both, so the caller must not
     *         also walk @p r; false when they stay two separate commands.
     */
    bool mergeDeferredResolver(Token& tok, Token& r)
    {
        const tree::CommandNode& d = tok.node.node();
        const tree::CommandNode& rn = r.node.node();

        // config: the bound field itself.
        const bool dHasConfig = d.hasConfig();
        const bool rHasConfig = rn.hasConfig();
        if (dHasConfig && rHasConfig && d.configId != rn.configId) return false;

        // enum: which member the field is set to, meaningless without config.
        const bool dHasEnum = d.hasEnumChange();
        const bool rHasEnum = rn.hasEnumChange();
        if (dHasEnum && rHasEnum && d.configExt != rn.configExt) return false;

        // pattern: the placeholder class a matched word belongs to.
        const Pattern dPattern = tok.pattern;
        const Pattern rPattern = r.pattern;
        const bool dHasPattern = dPattern != P_NONE;
        const bool rHasPattern = rPattern != P_NONE;
        if (dHasPattern && rHasPattern && dPattern != rPattern) return false;

        // value: the literal word matched, fixed keywords included -- a bare
        // `<cr>` carries none.
        const bool dHasValue = tok.node.name() != "<cr>";
        const bool rHasValue = r.node.name() != "<cr>";
        if (dHasValue && rHasValue && tok.value != r.value) return false;

        // Nothing conflicted, so whichever side uniquely supplies something
        // wins the merge; the resolver naming any of the four is the usual
        // case and takes the token over outright, matching what used to be
        // an unconditional replacement here.
        if ((rHasConfig && !dHasConfig) || (rHasEnum && !dHasEnum)
            || (rHasPattern && !dHasPattern) || (rHasValue && !dHasValue))
        {
            tok.node = r.node;
            tok.pattern = r.pattern;
        }

        return true;
    }

    /**
     * @brief Writes the tuples staged so far, and empties the staging list.
     *
     * Called wherever the write scope is about to move, and once when the line
     * runs out. A tuple is assembled from members named across a block and
     * cannot be written until the block ends, but it must not outlive it
     * either: the members named one scope's entry, and by the next command
     * that scope is gone.
     */
    bool commitStaged(std::vector<Token*>& staged)
    {
        if (staged.empty()) return true;

        const bool ok = execution::commitTuples(ctx, staged);
        staged.clear();
        return ok;
    }

    /**
     * @brief Groups the line into the scopes its commands write to.
     *
     * A line is written in whatever order reads well -- `router eigrp 1 vrf RED
     * network 10.0.0.0 redistribute static` names two scopes and moves between
     * them wherever the phrasing put the move. Executing in that order makes
     * every command's target depend on which rescopes happen to precede it,
     * which is why a tuple named across a rescope used to commit into the wrong
     * registry.
     *
     * So the line is grouped before it is run: everything writing the current
     * scope first, then each rescope with the commands it introduced, then mode
     * changes last. What a command writes then depends on the block it is in
     * rather than on where the phrasing put it.
     *
     * The grouping is stable, so within a block the line order is untouched --
     * which is what keeps a run's value tokens behind the keyword that binds
     * them, and the members of one tuple in the order they were typed.
     *
     * Mode changes go last because they are the one move that outlives the
     * line. Anything after one would be written in the entered mode rather than
     * the mode the line was typed in, so there is nothing that may follow.
     */
    void orderByScope(std::vector<Token*>& toks)
    {
        std::vector<Block> block(toks.size());

        Block current;
        uint32_t next = 1;

        for (size_t i = 0; i < toks.size(); i++)
        {
            Token* t = toks[i];
            if (!t) continue;

            if (t->modeFlagged() || t->deferredWrite) current = Block{ true, 0 };
            else if (t->registryFlagged()) current = Block{ false, next++ };

            block[i] = current;

            if (t->modeExit()) break;
        }

        std::vector<size_t> order(toks.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;

        std::stable_sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return block[a] < block[b]; });

        std::vector<Token*> sorted;
        sorted.reserve(toks.size());
        for (size_t i : order) sorted.push_back(toks[i]);

        toks.swap(sorted);
    }

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
     * from the context because a rescope earlier on the same line may have moved
     * ctx.ctx already, and the binding below has to be resolved against that
     * moved pointer while the return frame has to hold the one the line started
     * on -- see @ref RegistryScope::release.
     */
    bool handleModeChange(std::span<Token*> toks, Scope retTo)
    {
        const Token* binding = nullptr;
        for (Token* t : toks)
            if (t && t->hasNode()) binding = t;

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
                    if (!utils::resolveKey<Key>(toks, key) && !std::ranges::range<Key>) return;

                    if (ctx.negate || ctx.defaulted)
                    {
                        field.erase(key);
                        ok = true;
                        return;
                    }

                    auto& entered = *field.emplaceBack(key, binding->node.nodeIndex());
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
     * `end` unwinds every configuration mode where `exit` steps back one, so it
     * stops at the first mode that is not one -- PrivilegedExec, normally --
     * rather than at the bottom of the stack. Which of the two this is comes
     * from the command's name rather than a flag: both are the same kind of node
     * to the tree, and the grammar has one spelling for each.
     */
    bool handleModeExit(Token* t)
    {
        if (t && t->node.name() != "end") return nav.popMode();

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
         * `router eigrp 1` reads ROUTER_EIGRP out of the VRF that `eigrp`
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
    bool handleRegistryChange(std::span<Token*> toks)
    {
        const Token* binding = nullptr;
        for (Token* t : toks)
            if (t && t->hasNode()) binding = t;

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
                    bool resolved = utils::resolveKey<Key>(toks, key);
                    if (!resolved)
                    {
                        bool spelled = false;
                        for (const Token* t : toks)
                            if (t && t->pattern != P_NONE) { spelled = true; break; }
                        if (spelled || !std::is_default_constructible_v<Key>) return;
                    }

                    if (ctx.negate || ctx.defaulted)
                    {
                        field.erase(key);
                        ok = true;
                        return;
                    }

                    auto* movedPtr = field.emplaceBack(key, binding->node.nodeIndex());
                    auto& moved = *movedPtr;
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
    bool handleBitMapFlags(std::span<Token*> toks)
    {
        const tree::CommandNode& bound = toks.front()->node.node();

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
    bool handleValueChange(std::span<Token*> toks)
    {
        Token& value = *toks[0];
        Token* extra = toks.size() > 1 ? toks[1] : nullptr;

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
