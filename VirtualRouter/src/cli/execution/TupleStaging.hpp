/**
 * @file TupleStaging.hpp
 * @brief Collects the members of a tuple-valued field and writes them as one entry.
 *
 * Most commands write a single field, and @ref ExecutorUtils.hpp translates those
 * a token at a time. Tuple fields cannot work that way: `range A.B.C.D cost 100`
 * names two members of one list entry, and writing each as it is parsed would
 * insert two half-filled entries instead of one whole one.
 *
 * So the executor stages instead of writing. Each bound token contributes a
 * @ref TupleMember naming its field and its index within that field's tuple, and
 * nothing reaches the config until the line is done. At that point the members
 * are grouped by field, each group fills one default-constructed tuple, and the
 * result is inserted once. Members the line never named keep their defaults,
 * which is what makes `range A.B.C.D` and `range A.B.C.D cost 100` differ only
 * in the cost rather than in how many entries they leave behind.
 *
 * Staging is sequencing, not translation, which is why it lives apart from the
 * token-to-field helpers it calls into.
 */

#ifndef CLI_TUPLE_STAGING_HPP
#define CLI_TUPLE_STAGING_HPP

#include <algorithm>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "cli/execution/ExecutorUtils.hpp"
#include "cli/session/Token.hpp"
#include "cli/modes/Context.hpp"
#include "cli/tree/nodes/Command.h"

namespace cli::execution
{

/**
 * @brief One member of one tuple field, waiting to be written.
 *
 * The field is identified by its packed configId rather than by a resolved
 * accessor, because the accessor's type is only recoverable inside a
 * visitBound callback and cannot be carried out of one.
 */
struct TupleMember
{
    uint16_t configId;   ///< Which field this member belongs to.
    uint8_t  member;     ///< std::get index within that field's tuple.
    Token*   value;      ///< The token to translate; null for a bare flag.
};

/**
 * @brief Records the members a run names, without writing any of them.
 *
 * The binding rides the deepest node the parse reached, so within one run the
 * last bound token names the member and any token after it is its value.
 */
inline void stageTupleMembers(std::span<Token> toks, std::vector<TupleMember>& out)
{
    for (size_t i = 0; i < toks.size(); ++i)
    {
        if (!toks[i].tupChange()) continue;

        const tree::CommandNode& bound = toks[i].node.node();

        // A member's value is the next token along, when there is one that
        // is not itself a binding. A bool member has none: naming it is the
        // whole command, as in `range ... advertise`.
        Token* value = nullptr;
        if (i + 1 < toks.size() && !toks[i + 1].tupChange())
            value = &toks[i + 1];

        out.push_back({bound.configId, bound.configExt, value});
    }
}

/**
 * @brief Writes one member into a staging tuple, by runtime index.
 *
 * std::get needs the index at compile time, so the runtime one is matched
 * against an expansion over the tuple's size and the branch that matches
 * does the write.
 */
template <typename Node>
bool writeTupleMember(cli::ContextBase& ctx, Node& staging, TupleMember& m)
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>)
    {
        bool done = false;
        ([&]{
            if (m.member != Is) return;

            auto& elem = std::get<Is>(staging);
            using Elem = std::remove_cvref_t<decltype(elem)>;

            // A bool member is set by being named, so it takes no token and
            // must not be failed for arriving without one.
            if constexpr (std::is_same_v<Elem, bool>)
            {
                elem = !ctx.negate;
                done = true;
            }
            else if constexpr (utils::isOptional<Elem>::value)
            {
                typename Elem::value_type v{};
                if (utils::setTupleElement(v, m.value))
                {
                    elem = v;
                    done = true;
                }
            }
            else
            {
                done = utils::setTupleElement(elem, m.value);
            }
        }(), ...);
        return done;
    }(std::make_index_sequence<std::tuple_size_v<Node>>{});
}

/// @brief Fills one tuple from the members staged for a single field and inserts it.
inline bool commitOneTuple(cli::ContextBase& ctx, std::span<TupleMember> members)
{
    // Every member in the span shares a field, so any of them identifies it.
    const tree::CommandNode probe{ .configId = members[0].configId };

    bool ok = false;
    utils::visitBound(ctx, probe, [&](auto&& accessor)
    {
        using Visited = std::remove_cvref_t<decltype(accessor)>;

        if constexpr (requires { typename Visited::Field; })
        {
            using Field = typename Visited::Field;

            if constexpr (config::IsListField<Field>)
            {
                using Node = typename Field::node;

                if constexpr (utils::IsTuple<Node>::value)
                {
                    Node staging{};
                    bool filled = true;

                    for (TupleMember& m : members)
                        if (!writeTupleMember(ctx, staging, m))
                            filled = false;

                    if (filled)
                        ok = utils::setListEntry(accessor, ctx, staging);
                }
            }
        }
    });

    return ok;
}

/**
 * @brief Writes each staged field's members as one list entry.
 *
 * Members of the same field are gathered into a single staging tuple and
 * inserted once. Anything not named on the line keeps the tuple's default,
 * which is what makes `range A.B.C.D` and `range A.B.C.D cost 100` differ
 * only in the cost rather than in how many entries they leave behind.
 */
inline bool commitTuples(cli::ContextBase& ctx, std::vector<TupleMember>& staged)
{
    if (staged.empty()) return true;

    // Grouped by field rather than by adjacency: a line may name members of
    // two tuple fields in any order, and committing a field twice would
    // insert two half-filled entries instead of one whole one.
    std::stable_sort(staged.begin(), staged.end(),
        [](const TupleMember& a, const TupleMember& b)
        { return a.configId < b.configId; });

    bool ok = true;

    for (size_t i = 0; i < staged.size(); )
    {
        const uint16_t field = staged[i].configId;

        size_t end = i;
        while (end < staged.size() && staged[end].configId == field) ++end;

        if (!commitOneTuple(ctx, std::span<TupleMember>(staged).subspan(i, end - i)))
            ok = false;

        i = end;
    }

    return ok;
}
}

#endif // CLI_TUPLE_STAGING_HPP
